#!/usr/bin/env python3
import argparse
import json
import re
from bisect import bisect_left
from collections import Counter
import math
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple

try:
    import yaml  # type: ignore
except ImportError:  # pragma: no cover - fallback path for minimal env
    yaml = None


def load_yaml(path: Path) -> Dict[str, Any]:
    text = path.read_text(encoding="utf-8")
    try:
        return json.loads(text)
    except json.JSONDecodeError:
        pass

    if yaml is not None:
        return yaml.safe_load(text)

    raise RuntimeError(
        f"Cannot parse {path}. Install pyyaml or use JSON-compatible YAML content."
    )


def parse_csv_rows(csv_path: Path) -> List[Dict[str, str]]:
    rows: List[Dict[str, str]] = []
    if not csv_path.exists():
        return rows

    header: Optional[List[str]] = None
    with csv_path.open("r", encoding="utf-8", errors="ignore") as f:
        for raw_line in f:
            line = raw_line.strip()
            if not line:
                continue
            if line.startswith("Version:"):
                continue

            parts = [p.strip() for p in line.split(",")]
            if header is None:
                header = parts
                continue

            if len(parts) < len(header):
                continue
            rows.append(dict(zip(header, parts)))

    return rows


def read_jsonl(path: Path) -> List[Dict[str, Any]]:
    rows: List[Dict[str, Any]] = []
    if not path.exists():
        return rows
    with path.open("r", encoding="utf-8", errors="ignore") as f:
        for line in f:
            text = line.strip()
            if not text:
                continue
            try:
                obj = json.loads(text)
            except json.JSONDecodeError:
                continue
            if isinstance(obj, dict):
                rows.append(obj)
    return rows


def _parse_int(value: Optional[str]) -> Optional[int]:
    if value is None or value == "":
        return None
    try:
        return int(float(value))
    except ValueError:
        return None


def _parse_float(value: Optional[str]) -> Optional[float]:
    if value is None or value == "":
        return None
    try:
        return float(value)
    except ValueError:
        return None


def select_ego_rows(rows: List[Dict[str, str]]) -> List[Dict[str, str]]:
    if not rows:
        return []

    ids = [_parse_int(r.get("id")) for r in rows]
    valid_ids = [i for i in ids if i is not None]
    if not valid_ids:
        return rows

    target_id = 0 if 0 in valid_ids else min(valid_ids)
    return [r for r in rows if _parse_int(r.get("id")) == target_id]


def _best_non_ego_rows(rows: List[Dict[str, str]], ego_id: int) -> Tuple[Optional[int], List[Dict[str, str]]]:
    id_to_rows: Dict[int, List[Dict[str, str]]] = {}
    for row in rows:
        row_id = _parse_int(row.get("id"))
        if row_id is None or row_id == ego_id:
            continue
        id_to_rows.setdefault(row_id, []).append(row)

    if not id_to_rows:
        return None, []

    best_id = max(id_to_rows.items(), key=lambda kv: len(kv[1]))[0]
    return best_id, id_to_rows[best_id]


def _nearest_series_value(series: List[Tuple[float, float]], ts: float) -> Optional[float]:
    if not series:
        return None
    times = [t for t, _ in series]
    idx = bisect_left(times, ts)
    if idx <= 0:
        return series[0][1]
    if idx >= len(series):
        return series[-1][1]
    prev_t, prev_v = series[idx - 1]
    next_t, next_v = series[idx]
    if abs(ts - prev_t) <= abs(next_t - ts):
        return prev_v
    return next_v


def _moving_average_series(series: List[Tuple[float, float]], half_window: int = 5) -> List[Tuple[float, float]]:
    if not series:
        return []
    out: List[Tuple[float, float]] = []
    for idx, (ts, _) in enumerate(series):
        lo = max(0, idx - half_window)
        hi = min(len(series), idx + half_window + 1)
        window = series[lo:hi]
        mean_v = sum(v for _, v in window) / float(len(window))
        out.append((ts, mean_v))
    return out


def compute_kpis(feature_dir: Path) -> Dict[str, Any]:
    k: Dict[str, Any] = {}
    csv_path = feature_dir / "sim.csv"
    rows = parse_csv_rows(csv_path)
    ego_rows = select_ego_rows(rows)
    if not ego_rows:
        return k
    ego_id = _parse_int(ego_rows[0].get("id"))
    if ego_id is None:
        ego_id = 0

    times = [_parse_float(r.get("time")) for r in ego_rows]
    times = [t for t in times if t is not None]

    # Authoritative speed for validation:
    # derive actual speed from XY displacement over time instead of trusting
    # object speed column, which can diverge from RealVehicle physics state.
    actual_speeds: List[float] = []
    signed_speeds: List[float] = []
    actual_speed_series: List[Tuple[float, float]] = []
    signed_speed_series: List[Tuple[float, float]] = []
    for prev_row, row in zip(ego_rows, ego_rows[1:]):
        prev_t = _parse_float(prev_row.get("time"))
        t = _parse_float(row.get("time"))
        prev_x = _parse_float(prev_row.get("x"))
        x = _parse_float(row.get("x"))
        prev_y = _parse_float(prev_row.get("y"))
        y = _parse_float(row.get("y"))
        prev_s = _parse_float(prev_row.get("s"))
        s = _parse_float(row.get("s"))
        if None in (prev_t, t, prev_x, x, prev_y, y):
            continue
        dt = t - prev_t
        if dt <= 0.0:
            continue
        dxy = math.hypot(x - prev_x, y - prev_y)
        actual_speed = dxy / dt
        actual_speeds.append(actual_speed)
        actual_speed_series.append((t, actual_speed))
        if None not in (prev_s, s):
            signed_speed = (s - prev_s) / dt
            signed_speeds.append(signed_speed)
            signed_speed_series.append((t, signed_speed))

    if times:
        k["duration_s"] = max(times)
    if actual_speeds:
        k["speed_source"] = "xy_derivative"
        k["speed_min_mps"] = min(actual_speeds)
        k["speed_max_mps"] = max(actual_speeds)
        k["speed_end_mps"] = actual_speeds[-1]
    if signed_speeds:
        k["signed_speed_source"] = "s_derivative"
        k["signed_speed_min_mps"] = min(signed_speeds)
        k["signed_speed_max_mps"] = max(signed_speeds)
        k["signed_speed_end_mps"] = signed_speeds[-1]

    lane_ids = [_parse_int(r.get("laneId")) for r in ego_rows]
    lane_ids = [l for l in lane_ids if l is not None]
    if lane_ids:
        k["lane_id_start"] = lane_ids[0]
        k["lane_id_end"] = lane_ids[-1]
        lane_change_count = 0
        prev_lane = lane_ids[0]
        for lane_id in lane_ids[1:]:
            if lane_id != prev_lane:
                lane_change_count += 1
                prev_lane = lane_id
        k["lane_change_count"] = lane_change_count

    s_values = [_parse_float(r.get("s")) for r in ego_rows]
    s_values = [s for s in s_values if s is not None]
    if s_values:
        k["s_start_m"] = s_values[0]
        k["s_end_m"] = s_values[-1]
        k["s_progress_m"] = s_values[-1] - s_values[0]

    t_values = [_parse_float(r.get("t")) for r in ego_rows]
    t_values = [t for t in t_values if t is not None]
    if t_values:
        k["t_min_m"] = min(t_values)
        k["t_max_m"] = max(t_values)
        k["t_span_m"] = max(t_values) - min(t_values)
        k["t_abs_max_m"] = max(abs(v) for v in t_values)

    road_ids = [_parse_int(r.get("roadId")) for r in ego_rows]
    road_ids = [rid for rid in road_ids if rid is not None]
    if road_ids:
        k["road_id_mode"] = Counter(road_ids).most_common(1)[0][0]
        road_id_change_count = 0
        prev_road = road_ids[0]
        for road_id in road_ids[1:]:
            if road_id != prev_road:
                road_id_change_count += 1
                prev_road = road_id
        k["road_id_change_count"] = road_id_change_count

    # Detect "moving in XY while road-progress s is frozen" -> often indicates
    # path/control breakage near/off road.
    s_stall_time = 0.0
    duration = 0.0
    xy_path_length = 0.0
    s_stall_eps_m = 1e-4
    xy_move_eps_m = 0.02
    speed_gate_mps = 2.0
    for prev_row, row in zip(ego_rows, ego_rows[1:]):
        prev_t = _parse_float(prev_row.get("time"))
        t = _parse_float(row.get("time"))
        prev_s = _parse_float(prev_row.get("s"))
        s = _parse_float(row.get("s"))
        prev_x = _parse_float(prev_row.get("x"))
        x = _parse_float(row.get("x"))
        prev_y = _parse_float(prev_row.get("y"))
        y = _parse_float(row.get("y"))
        if None in (prev_t, t, prev_s, s, prev_x, x, prev_y, y):
            continue

        dt = max(0.0, t - prev_t)
        duration += dt
        dxy = math.hypot(x - prev_x, y - prev_y)
        actual_speed = (dxy / dt) if dt > 0.0 else 0.0
        xy_path_length += dxy
        if abs(s - prev_s) < s_stall_eps_m and dxy > xy_move_eps_m and abs(actual_speed) > speed_gate_mps:
            s_stall_time += dt

    if duration > 0.0:
        k["s_stall_time_s"] = s_stall_time
        k["s_stall_ratio"] = s_stall_time / duration
    k["xy_path_length_m"] = xy_path_length

    if actual_speeds:
        k["speed_span_mps"] = max(actual_speeds) - min(actual_speeds)
        sorted_speeds = sorted(actual_speeds)
        p90_idx = max(0, min(len(sorted_speeds) - 1, int(0.9 * (len(sorted_speeds) - 1))))
        v_ref = sorted_speeds[p90_idx]
        baseline_count = max(5, min(20, len(actual_speeds) // 5))
        baseline_speed = sum(actual_speeds[:baseline_count]) / float(baseline_count)
        dv = v_ref - baseline_speed
        if dv > 1.0 and actual_speed_series:
            accel_start_threshold = baseline_speed + (0.2 * dv)
            settle_band = max(0.5, 0.1 * dv)

            accel_start_idx = None
            for idx, (_, v) in enumerate(actual_speed_series):
                if v >= accel_start_threshold:
                    accel_start_idx = idx
                    break

            settle_idx = None
            if accel_start_idx is not None:
                consecutive_target = 8
                hit = 0
                for idx in range(accel_start_idx, len(actual_speed_series)):
                    _, v = actual_speed_series[idx]
                    if abs(v - v_ref) <= settle_band:
                        hit += 1
                        if hit >= consecutive_target:
                            settle_idx = idx - consecutive_target + 1
                            break
                    else:
                        hit = 0

            if accel_start_idx is not None and settle_idx is not None:
                t0, v0 = actual_speed_series[accel_start_idx]
                t_settle, v_settle = actual_speed_series[settle_idx]
                dt_accel = max(1e-6, t_settle - t0)
                k["accel_phase_mean_acc"] = (v_settle - v0) / dt_accel
                k["settle_time_s"] = max(0.0, t_settle - actual_speed_series[0][0])
                k["overshoot_ratio"] = max(0.0, (max(actual_speeds) - v_ref) / max(1e-6, dv))
                steady_vals = [v for _, v in actual_speed_series[settle_idx:]]
                if len(steady_vals) >= 3:
                    mean_steady = sum(steady_vals) / float(len(steady_vals))
                    variance = sum((v - mean_steady) ** 2 for v in steady_vals) / float(len(steady_vals))
                    k["steady_state_std_mps"] = math.sqrt(variance)

    lead_id, lead_rows = _best_non_ego_rows(rows, ego_id)
    if lead_id is not None and lead_rows:
        lead_by_time = {}
        for row in lead_rows:
            t = _parse_float(row.get("time"))
            s = _parse_float(row.get("s"))
            if t is None or s is None:
                continue
            lead_by_time[round(t, 3)] = s

        gaps = []
        for row in ego_rows:
            t = _parse_float(row.get("time"))
            ego_s = _parse_float(row.get("s"))
            if t is None or ego_s is None:
                continue
            lead_s = lead_by_time.get(round(t, 3))
            if lead_s is None:
                continue
            gaps.append(lead_s - ego_s)

        if gaps:
            k["lead_id"] = lead_id
            k["lead_gap_start_m"] = gaps[0]
            k["lead_gap_min_m"] = min(gaps)
            k["lead_gap_max_m"] = max(gaps)
            k["lead_gap_end_m"] = gaps[-1]
            k["lead_gap_mean_m"] = sum(gaps) / len(gaps)

    osi_light_metrics_path = feature_dir / "osi_light_metrics.json"
    if osi_light_metrics_path.exists():
        try:
            light_metrics = json.loads(osi_light_metrics_path.read_text(encoding="utf-8"))
            if isinstance(light_metrics, dict):
                for key in (
                    "brake_on_ratio",
                    "reverse_on_ratio",
                    "brake_on_duration_s",
                    "reverse_on_duration_s",
                    "light_state_observed",
                    "host_vehicle_id_seen",
                    "host_vehicle_id_last",
                    "host_vehicle_match_frames",
                    "unmatched_frames",
                    "moving_object_ids_seen",
                ):
                    if key in light_metrics:
                        k[key] = light_metrics[key]
        except json.JSONDecodeError:
            pass

    osi_lights_csv_path = feature_dir / "osi_lights.csv"
    light_rows = parse_csv_rows(osi_lights_csv_path)
    smoothed_actual_speed_series = _moving_average_series(actual_speed_series, half_window=5)
    baseline_samples = [v for _, v in smoothed_actual_speed_series[:50]]
    baseline_speed = (sum(baseline_samples) / float(len(baseline_samples))) if baseline_samples else 0.0
    actual_accel_series: List[Tuple[float, float]] = []
    for (t0, v0), (t1, v1) in zip(smoothed_actual_speed_series, smoothed_actual_speed_series[1:]):
        dt = t1 - t0
        if dt <= 0.0:
            continue
        actual_accel_series.append((t1, (v1 - v0) / dt))

    reverse_window_samples = 0
    reverse_on_in_reverse_window = 0
    braking_window_samples = 0
    brake_on_in_braking_window = 0
    for row in light_rows:
        ts = _parse_float(row.get("timestamp_s"))
        if ts is None or ts < 0.0:
            continue
        signed_speed = _nearest_series_value(signed_speed_series, ts)
        if signed_speed is None:
            continue
        brake_on = _parse_int(row.get("brake_on")) == 1
        reverse_on = _parse_int(row.get("reverse_on")) == 1

        # Reverse window: actual reverse motion region only.
        if signed_speed <= -0.5:
            reverse_window_samples += 1
            if reverse_on:
                reverse_on_in_reverse_window += 1

        # Braking window: forward motion with clearly negative longitudinal accel.
        # Use XY-speed derivative instead of ds/dt second derivative to avoid s-quantization artifacts.
        longitudinal_accel = _nearest_series_value(actual_accel_series, ts)
        smoothed_speed = _nearest_series_value(smoothed_actual_speed_series, ts)
        if (
            signed_speed >= 0.5
            and longitudinal_accel is not None
            and smoothed_speed is not None
            and longitudinal_accel <= -0.5
            and smoothed_speed <= (baseline_speed - 0.3)
        ):
            braking_window_samples += 1
            if brake_on:
                brake_on_in_braking_window += 1

    if reverse_window_samples > 0:
        k["reverse_on_ratio_in_reverse_window"] = float(reverse_on_in_reverse_window) / float(reverse_window_samples)
    if braking_window_samples > 0:
        k["brake_on_ratio_in_braking_window"] = float(brake_on_in_braking_window) / float(braking_window_samples)
    k["reverse_window_samples"] = reverse_window_samples
    k["braking_window_samples"] = braking_window_samples

    return k


def evaluate_kpi_checks(
    kpi: Dict[str, Any],
    checks: List[Dict[str, Any]],
) -> Dict[str, Any]:
    details: List[Dict[str, Any]] = []
    all_ok = True

    for raw_check in checks:
        check = dict(raw_check)
        metric = str(check.get("metric", ""))
        actual = kpi.get(metric)

        detail: Dict[str, Any] = {"metric": metric, "actual": actual}
        ok = True

        if actual is None:
            ok = False
            detail["reason"] = "metric_missing"
        else:
            if "min" in check:
                min_value = float(check["min"])
                detail["min"] = min_value
                ok = ok and float(actual) >= min_value
            if "max" in check:
                max_value = float(check["max"])
                detail["max"] = max_value
                ok = ok and float(actual) <= max_value
            if "equals" in check:
                expected = check["equals"]
                detail["equals"] = expected
                ok = ok and (actual == expected)
            if "in" in check:
                values = list(check["in"])
                detail["in"] = values
                ok = ok and (actual in values)
            if "target" in check:
                target = float(check["target"])
                tol = float(check.get("tolerance", 0.0))
                diff = abs(float(actual) - target)
                detail["target"] = target
                detail["tolerance"] = tol
                detail["abs_error"] = diff
                ok = ok and diff <= tol

        detail["pass"] = ok
        if not ok and metric in {
            "light_state_observed",
            "brake_on_ratio",
            "reverse_on_ratio",
            "brake_on_ratio_in_braking_window",
            "reverse_on_ratio_in_reverse_window",
        }:
            for diag_key in (
                "host_vehicle_id_seen",
                "host_vehicle_id_last",
                "host_vehicle_match_frames",
                "unmatched_frames",
                "moving_object_ids_seen",
                "reverse_window_samples",
                "braking_window_samples",
            ):
                if diag_key in kpi:
                    detail[diag_key] = kpi[diag_key]
        details.append(detail)
        all_ok = all_ok and ok

    return {"pass": all_ok, "details": details}


def evaluate_kpi_checks_any(
    kpi: Dict[str, Any],
    checks_any: List[Dict[str, Any]],
) -> Dict[str, Any]:
    if not checks_any:
        return {"pass": True, "details": [], "mode": "any", "matched_count": 0}

    details: List[Dict[str, Any]] = []
    matched = 0
    for raw_check in checks_any:
        single_eval = evaluate_kpi_checks(kpi, [dict(raw_check)])
        detail = single_eval["details"][0]
        details.append(detail)
        if detail.get("pass", False):
            matched += 1

    return {
        "pass": matched > 0,
        "details": details,
        "mode": "any",
        "matched_count": matched,
    }


def evaluate_trace_integrity(feature_id: str, feature_dir: Path) -> Dict[str, Any]:
    if feature_id != "F01":
        return {
            "required": False,
            "pass": True,
            "stats": {},
            "mismatch_samples": [],
        }

    cpp_rows = read_jsonl(feature_dir / "cpp_to_py_trace.jsonl")
    py_rows = read_jsonl(feature_dir / "py_to_cpp_trace.jsonl")
    py_script_rows = read_jsonl(feature_dir / "python_trace.jsonl")
    mismatches: List[str] = []

    if not cpp_rows or not py_rows or not py_script_rows:
        return {
            "required": True,
            "pass": False,
            "stats": {
                "cpp_to_py_count": len(cpp_rows),
                "py_to_cpp_count": len(py_rows),
                "python_trace_count": len(py_script_rows),
            },
            "mismatch_samples": ["trace_missing"],
        }

    cpp_ids = [int(r.get("frame_id", -1)) for r in cpp_rows]
    py_ids = [int(r.get("frame_id", -1)) for r in py_rows]
    py_script_ids = [int(r.get("frame_id", -1)) for r in py_script_rows]

    def _is_strict_sequence(ids: List[int]) -> bool:
        if not ids:
            return False
        start = ids[0]
        return all(v == start + i for i, v in enumerate(ids))

    if not _is_strict_sequence(cpp_ids):
        mismatches.append("trace_frame_gap_cpp_to_py")
    if not _is_strict_sequence(py_ids):
        mismatches.append("trace_frame_gap_py_to_cpp")
    if not _is_strict_sequence(py_script_ids):
        mismatches.append("trace_frame_gap_python_trace")

    if len(cpp_rows) != len(py_rows) or len(cpp_rows) != len(py_script_rows):
        mismatches.append("trace_count_mismatch")
    if set(cpp_ids) != set(py_ids) or set(cpp_ids) != set(py_script_ids):
        mismatches.append("trace_frame_mismatch")

    gt_ok = all(int(r.get("gt_size", 0)) > 0 for r in cpp_rows)
    waypoints_ok = all(int(r.get("waypoint_count", 0)) > 0 for r in cpp_rows)
    lon_ok = all(int(r.get("lon_profile_count", 0)) > 0 for r in cpp_rows)
    if not gt_ok:
        mismatches.append("trace_payload_missing_gt")
    if not waypoints_ok:
        mismatches.append("trace_payload_missing_waypoints")
    if not lon_ok:
        mismatches.append("trace_payload_missing_lon_profile")

    required_keys_ok = True
    for r in py_rows:
        req = r.get("required_keys", {})
        if not isinstance(req, dict):
            required_keys_ok = False
            break
        for key in ("throttle", "brake", "steering", "gear", "lights"):
            if req.get(key) is not True:
                required_keys_ok = False
                break
        if not required_keys_ok:
            break
    if not required_keys_ok:
        mismatches.append("trace_result_key_missing")

    range_ok = True
    for r in py_rows:
        if r.get("error"):
            range_ok = False
            mismatches.append("trace_result_error")
            break
        parsed = r.get("parsed", {})
        if not isinstance(parsed, dict):
            range_ok = False
            break
        throttle = parsed.get("throttle")
        brake = parsed.get("brake")
        steering = parsed.get("steering")
        gear = parsed.get("gear")
        try:
            if not (0.0 <= float(throttle) <= 1.0):
                range_ok = False
            if not (0.0 <= float(brake) <= 1.0):
                range_ok = False
            if not (-1.0 <= float(steering) <= 1.0):
                range_ok = False
            if int(gear) < -1:
                range_ok = False
        except (TypeError, ValueError):
            range_ok = False
        if not range_ok:
            break
    if not range_ok:
        mismatches.append("trace_result_out_of_range")

    stats = {
        "cpp_to_py_count": len(cpp_rows),
        "py_to_cpp_count": len(py_rows),
        "python_trace_count": len(py_script_rows),
        "gt_positive_ratio": (sum(1 for r in cpp_rows if int(r.get("gt_size", 0)) > 0) / len(cpp_rows)) if cpp_rows else 0.0,
        "waypoint_positive_ratio": (sum(1 for r in cpp_rows if int(r.get("waypoint_count", 0)) > 0) / len(cpp_rows)) if cpp_rows else 0.0,
        "lon_profile_positive_ratio": (sum(1 for r in cpp_rows if int(r.get("lon_profile_count", 0)) > 0) / len(cpp_rows)) if cpp_rows else 0.0,
    }

    return {
        "required": True,
        "pass": len(mismatches) == 0,
        "stats": stats,
        "mismatch_samples": mismatches[:10],
    }


def _build_expected_light_series(py_trace: List[Dict[str, Any]]) -> Tuple[List[Dict[str, Any]], float]:
    dt_values: List[float] = []
    expected_series: List[Dict[str, Any]] = []
    current = {
        "low_beam": 0,
        "high_beam": 0,
        "left_indicator": 0,
        "right_indicator": 0,
        "fog": 0,
    }

    ordered_rows = sorted(py_trace, key=lambda r: int(r.get("frame_id", -1)))
    for row in ordered_rows:
        frame_id = int(row.get("frame_id", -1))
        recv = row.get("recv", {})
        send = row.get("send", {})
        if isinstance(recv, dict):
            try:
                dt = float(recv.get("dt", 0.0))
                if dt > 0.0:
                    dt_values.append(dt)
            except (TypeError, ValueError):
                pass
        if not isinstance(send, dict):
            continue

        lights = send.get("lights", {})
        if isinstance(lights, dict):
            for key in current.keys():
                val = lights.get(key)
                if val is None:
                    continue
                token = str(val).strip().lower()
                if token == "on":
                    current[key] = 1
                elif token == "off":
                    current[key] = 0
                elif token == "auto":
                    current[key] = 0

        try:
            brake = float(send.get("brake", 0.0))
            gear = int(send.get("gear", 1))
        except (TypeError, ValueError):
            continue

        if current["left_indicator"] and current["right_indicator"]:
            indicator = "WARNING"
        elif current["left_indicator"]:
            indicator = "LEFT"
        elif current["right_indicator"]:
            indicator = "RIGHT"
        else:
            indicator = "OFF"

        expected_series.append(
            {
                "frame_id": frame_id,
                "head_light_on": current["low_beam"],
                "high_beam_on": current["high_beam"],
                "indicator_state": indicator,
                "fog_on": current["fog"],
                "brake_on": int(brake > 0.05),
                "reverse_on": int(gear == -1),
            }
        )

    dt_est = (sum(dt_values) / float(len(dt_values))) if dt_values else 0.01
    return expected_series, max(dt_est, 1e-3)


def evaluate_f02_light_mapping(feature_id: str, feature_dir: Path) -> Dict[str, Any]:
    if feature_id != "F02":
        return {"required": False, "pass": True, "kpi": {}, "stats": {}, "mismatch_samples": []}

    py_trace = read_jsonl(feature_dir / "python_trace.jsonl")
    osi_rows = parse_csv_rows(feature_dir / "osi_lights.csv")
    mismatches: List[str] = []
    if not py_trace or not osi_rows:
        return {
            "required": True,
            "pass": False,
            "kpi": {"light_mapping_match_ratio": 0.0, "light_mapping_latency_frames_max": 999, "light_mapping_missing_samples": max(1, len(py_trace))},
            "stats": {"python_trace_count": len(py_trace), "osi_lights_count": len(osi_rows)},
            "mismatch_samples": ["light_trace_missing"],
        }

    expected_series, dt_est = _build_expected_light_series(py_trace)
    if not expected_series:
        return {
            "required": True,
            "pass": False,
            "kpi": {"light_mapping_match_ratio": 0.0, "light_mapping_latency_frames_max": 999, "light_mapping_missing_samples": 1},
            "stats": {"python_trace_count": len(py_trace), "osi_lights_count": len(osi_rows)},
            "mismatch_samples": ["light_mapping_missing_samples"],
        }

    actual_series: List[Dict[str, Any]] = []
    for row in osi_rows:
        ts = _parse_float(row.get("timestamp_s"))
        if ts is None:
            continue
        actual_series.append(
            {
                "t": ts,
                "head_light_on": int(_parse_int(row.get("head_light_on")) or 0),
                "high_beam_on": int(_parse_int(row.get("high_beam_on")) or 0),
                "indicator_state": str(row.get("indicator_state", "UNKNOWN")).upper(),
                "fog_on": int(_parse_int(row.get("fog_on")) or 0),
                "brake_on": int(_parse_int(row.get("brake_on")) or 0),
                "reverse_on": int(_parse_int(row.get("reverse_on")) or 0),
            }
        )
    if not actual_series:
        return {
            "required": True,
            "pass": False,
            "kpi": {"light_mapping_match_ratio": 0.0, "light_mapping_latency_frames_max": 999, "light_mapping_missing_samples": len(expected_series)},
            "stats": {"python_trace_count": len(py_trace), "osi_lights_count": len(osi_rows)},
            "mismatch_samples": ["light_trace_missing"],
        }

    actual_series.sort(key=lambda x: float(x["t"]))
    start_frame = int(expected_series[0]["frame_id"])
    max_obs_t = float(actual_series[-1]["t"])
    missing_samples = 0
    match_count = 0
    mismatch_details: List[str] = []
    fields = ("head_light_on", "high_beam_on", "indicator_state", "fog_on", "brake_on", "reverse_on")
    expected_in_window: List[Dict[str, Any]] = []

    for e in expected_series:
        frame_id = int(e["frame_id"])
        t_exp = (frame_id - start_frame) * dt_est
        if t_exp > (max_obs_t + dt_est):
            continue
        expected_in_window.append(e)
        nearest = min(actual_series, key=lambda a: abs(float(a["t"]) - t_exp))
        if abs(float(nearest["t"]) - t_exp) > dt_est:
            missing_samples += 1
            if len(mismatch_details) < 5:
                mismatch_details.append(f"missing@frame={frame_id}")
            continue

        sample_match = True
        for field in fields:
            if nearest[field] != e[field]:
                sample_match = False
                if len(mismatch_details) < 5:
                    mismatch_details.append(f"mismatch@frame={frame_id}:{field}:exp={e[field]} actual={nearest[field]}")
        if sample_match:
            match_count += 1

    total_samples = len(expected_in_window)
    match_ratio = (float(match_count) / float(total_samples)) if total_samples > 0 else 0.0
    latency_frames_max = 0
    latency_fail = False
    for field in fields:
        if not expected_in_window:
            break
        prev = expected_in_window[0][field]
        for e in expected_in_window[1:]:
            cur = e[field]
            if cur == prev:
                continue
            frame_id = int(e["frame_id"])
            t_change = (frame_id - start_frame) * dt_est
            candidates = [a for a in actual_series if float(a["t"]) >= t_change and a[field] == cur]
            if not candidates:
                latency_fail = True
                if len(mismatch_details) < 5:
                    mismatch_details.append(f"latency_missing:{field}@frame={frame_id}")
                prev = cur
                continue
            t_match = float(candidates[0]["t"])
            latency_frames = max(0, int(round((t_match - t_change) / dt_est)))
            latency_frames_max = max(latency_frames_max, latency_frames)
            prev = cur

    if match_ratio < 1.0:
        mismatches.append("light_mapping_mismatch")
    if missing_samples > 0:
        mismatches.append("light_mapping_missing_samples")
    if latency_fail or latency_frames_max > 1:
        mismatches.append("light_mapping_latency_exceeded")

    return {
        "required": True,
        "pass": len(mismatches) == 0,
        "kpi": {
            "light_mapping_match_ratio": match_ratio,
            "light_mapping_latency_frames_max": latency_frames_max if not latency_fail else 999,
            "light_mapping_missing_samples": missing_samples,
        },
        "stats": {
            "python_trace_count": len(py_trace),
            "osi_lights_count": len(actual_series),
            "match_count": match_count,
            "total_samples": total_samples,
        },
        "mismatch_samples": (mismatches + mismatch_details)[:10],
    }


def evaluate_f03_autolight(feature_id: str, feature_dir: Path) -> Dict[str, Any]:
    if feature_id != "F03":
        return {"required": False, "pass": True, "kpi": {}, "stats": {}, "mismatch_samples": []}

    sim_rows = parse_csv_rows(feature_dir / "sim.csv")
    osi_rows = parse_csv_rows(feature_dir / "osi_lights.csv")
    if not sim_rows or not osi_rows:
        return {
            "required": True,
            "pass": False,
            "kpi": {
                "indicator_on_ratio_in_lanechange_window": 0.0,
                "indicator_off_ratio_outside_lanechange_window": 0.0,
                "indicator_dominant_side_ratio_in_lanechange_window": 0.0,
                "light_transition_latency_frames_max": 999,
                "light_missing_samples": 1,
            },
            "stats": {"sim_rows": len(sim_rows), "osi_rows": len(osi_rows)},
            "mismatch_samples": ["autolight_trace_missing"],
        }

    # Build signed speed + lane change events from sim.csv.
    sim_points: List[Tuple[float, float, int]] = []
    for r in sim_rows:
        t = _parse_float(r.get("time"))
        s = _parse_float(r.get("s"))
        lane = _parse_int(r.get("lane_id"))
        if lane is None:
            lane = _parse_int(r.get("laneId"))
        if t is None or s is None or lane is None:
            continue
        sim_points.append((t, s, lane))
    sim_points.sort(key=lambda x: x[0])

    signed_speed: List[Tuple[float, float]] = []
    lane_change_times: List[float] = []
    prev_lane: Optional[int] = None
    for i in range(1, len(sim_points)):
        t0, s0, lane0 = sim_points[i - 1]
        t1, s1, lane1 = sim_points[i]
        dt = t1 - t0
        if dt > 0.0:
            signed_speed.append((t1, (s1 - s0) / dt))
        if prev_lane is None:
            prev_lane = lane0
        if lane1 != prev_lane:
            lane_change_times.append(t1)
            prev_lane = lane1

    if not signed_speed:
        return {
            "required": True,
            "pass": False,
            "kpi": {
                "indicator_on_ratio_in_lanechange_window": 0.0,
                "indicator_off_ratio_outside_lanechange_window": 0.0,
                "indicator_dominant_side_ratio_in_lanechange_window": 0.0,
                "light_transition_latency_frames_max": 999,
                "light_missing_samples": len(osi_rows),
            },
            "stats": {"sim_rows": len(sim_rows), "osi_rows": len(osi_rows)},
            "mismatch_samples": ["autolight_signed_speed_missing"],
        }

    dt_est = 0.01
    timestamps = [_parse_float(r.get("timestamp_s")) for r in osi_rows]
    valid_timestamps = [t for t in timestamps if t is not None]
    if len(valid_timestamps) > 2:
        diffs = [valid_timestamps[i] - valid_timestamps[i - 1] for i in range(1, len(valid_timestamps))]
        diffs = [d for d in diffs if d > 0.0]
        if diffs:
            dt_est = max(1e-3, sum(diffs) / float(len(diffs)))

    reverse_window_samples = 0
    reverse_on_in_reverse_window = 0
    reverse_on_outside = 0
    outside_reverse_samples = 0

    indicator_on_in_lanechange = 0
    indicator_samples_in_lanechange = 0
    indicator_left_in_lanechange = 0
    indicator_right_in_lanechange = 0
    indicator_off_outside_lanechange = 0
    outside_lanechange_samples = 0
    light_missing_samples = 0

    def _in_lanechange_window(ts: float) -> bool:
        return any((lc - 1.0) <= ts <= (lc + 1.0) for lc in lane_change_times)

    actual_rows: List[Dict[str, Any]] = []
    for row in osi_rows:
        ts = _parse_float(row.get("timestamp_s"))
        if ts is None:
            continue
        s_speed = _nearest_series_value(signed_speed, ts)
        if s_speed is None:
            light_missing_samples += 1
            continue

        indicator = str(row.get("indicator_state", "OFF")).upper()
        reverse_on = _parse_int(row.get("reverse_on")) == 1

        if s_speed <= -0.5:
            reverse_window_samples += 1
            if reverse_on:
                reverse_on_in_reverse_window += 1
        else:
            outside_reverse_samples += 1
            if reverse_on:
                reverse_on_outside += 1

        if _in_lanechange_window(ts):
            indicator_samples_in_lanechange += 1
            if indicator != "OFF":
                indicator_on_in_lanechange += 1
            if indicator == "LEFT":
                indicator_left_in_lanechange += 1
            elif indicator == "RIGHT":
                indicator_right_in_lanechange += 1
            elif indicator == "WARNING":
                indicator_left_in_lanechange += 1
                indicator_right_in_lanechange += 1
        else:
            outside_lanechange_samples += 1
            if indicator == "OFF":
                indicator_off_outside_lanechange += 1

        actual_rows.append({"t": ts, "indicator": indicator, "reverse_on": reverse_on})

    reverse_on_ratio = (float(reverse_on_in_reverse_window) / float(reverse_window_samples)) if reverse_window_samples > 0 else 0.0
    reverse_off_outside_ratio = (float(outside_reverse_samples - reverse_on_outside) / float(outside_reverse_samples)) if outside_reverse_samples > 0 else 0.0
    indicator_on_ratio = (float(indicator_on_in_lanechange) / float(indicator_samples_in_lanechange)) if indicator_samples_in_lanechange > 0 else 0.0
    indicator_off_outside_ratio = (float(indicator_off_outside_lanechange) / float(outside_lanechange_samples)) if outside_lanechange_samples > 0 else 0.0

    dominant_count = max(indicator_left_in_lanechange, indicator_right_in_lanechange)
    dominant_side_ratio = (float(dominant_count) / float(max(1, indicator_on_in_lanechange))) if indicator_on_in_lanechange > 0 else 0.0

    # latency from lane change window start to first indicator ON
    latency_frames_max = 0
    for lc in lane_change_times:
        window_start = lc - 1.0
        candidates = [r for r in actual_rows if r["t"] >= window_start and r["t"] <= lc + 1.0 and r["indicator"] != "OFF"]
        if not candidates:
            latency_frames_max = 999
            break
        latency = max(0, int(round((candidates[0]["t"] - window_start) / dt_est)))
        latency_frames_max = max(latency_frames_max, latency)

    mismatches: List[str] = []
    if reverse_window_samples == 0:
        mismatches.append("reverse_window_missing")
    if indicator_samples_in_lanechange == 0:
        mismatches.append("lanechange_window_missing")
    if light_missing_samples > 0:
        mismatches.append("light_missing_samples")

    kpi = {
        "reverse_on_ratio_in_reverse_window": reverse_on_ratio,
        "reverse_on_ratio_outside_reverse_window": 1.0 - reverse_off_outside_ratio,
        "indicator_on_ratio_in_lanechange_window": indicator_on_ratio,
        "indicator_off_ratio_outside_lanechange_window": indicator_off_outside_ratio,
        "indicator_dominant_side_ratio_in_lanechange_window": dominant_side_ratio,
        "light_transition_latency_frames_max": latency_frames_max,
        "light_missing_samples": light_missing_samples,
    }
    stats = {
        "lane_change_events": len(lane_change_times),
        "reverse_window_samples": reverse_window_samples,
        "indicator_samples_in_lanechange": indicator_samples_in_lanechange,
    }
    return {
        "required": True,
        "pass": len(mismatches) == 0 and latency_frames_max <= 2,
        "kpi": kpi,
        "stats": stats,
        "mismatch_samples": mismatches[:10],
    }


def _fmt_num(value: Any) -> str:
    if isinstance(value, float):
        text = f"{value:.6f}".rstrip("0").rstrip(".")
        return text if text else "0"
    return str(value)


def _describe_check_nl(check: Dict[str, Any]) -> str:
    metric = str(check.get("metric", "unknown_metric"))
    parts: List[str] = []
    if "target" in check:
        target = _fmt_num(check.get("target"))
        tol = _fmt_num(check.get("tolerance", 0))
        parts.append(f"{metric} が {target}±{tol} の範囲に入ること")
    if "equals" in check:
        parts.append(f"{metric} が {_fmt_num(check.get('equals'))} に一致すること")
    if "min" in check:
        parts.append(f"{metric} が {_fmt_num(check.get('min'))} 以上であること")
    if "max" in check:
        parts.append(f"{metric} が {_fmt_num(check.get('max'))} 以下であること")
    if "in" in check:
        values = ", ".join(_fmt_num(v) for v in list(check.get("in", [])))
        parts.append(f"{metric} が [{values}] のいずれかであること")
    return "かつ ".join(parts) if parts else f"{metric} の条件を満たすこと"


def build_judgement_criteria_nl(
    feat: Dict[str, Any],
    and_checks: List[Dict[str, Any]],
    any_checks: List[Dict[str, Any]],
) -> List[str]:
    criteria: List[str] = []

    required_patterns = list(feat.get("required_patterns", []))
    forbidden_patterns = list(feat.get("forbidden_patterns", []))
    if required_patterns:
        patterns = " / ".join(str(p) for p in required_patterns)
        criteria.append(f"ログに次の必須パターンが出現すること: {patterns}")
    if forbidden_patterns:
        patterns = " / ".join(str(p) for p in forbidden_patterns)
        criteria.append(f"ログに次の禁止パターンが出現しないこと: {patterns}")

    for check in and_checks:
        criteria.append(_describe_check_nl(dict(check)))

    if any_checks:
        any_text = "、または ".join(_describe_check_nl(dict(check)) for check in any_checks)
        criteria.append(f"次のいずれかを満たすこと: {any_text}")

    if not criteria:
        criteria.append("ログとKPIに重大な異常がなく、シナリオを完走すること。")
    return criteria


def compare_with_golden(actual_kpi: Dict[str, Any], golden_file: Path, thresholds: Dict[str, Any]) -> Dict[str, Any]:
    if not golden_file.exists():
        return {"golden_available": False, "pass": True, "details": []}

    with golden_file.open("r", encoding="utf-8") as f:
        golden = json.load(f)

    details: List[Dict[str, Any]] = []
    ok = True
    speed_thr = float(thresholds.get("speed_rmse_mps", 0.30))
    s_end_thr = float(thresholds.get("road_s_end_delta_m", 1.0))
    s_progress_thr = float(thresholds.get("road_s_progress_delta_m", 1.0))
    t_abs_thr = float(thresholds.get("road_t_abs_max_delta_m", 0.25))

    numeric_metric_threshold_map = {
        "speed_max_mps": speed_thr,
        "speed_min_mps": speed_thr,
        "s_end_m": s_end_thr,
        "s_progress_m": s_progress_thr,
        "t_abs_max_m": t_abs_thr,
    }
    categorical_metrics = ["lane_id_end", "lane_change_count", "road_id_mode"]

    for metric, limit in numeric_metric_threshold_map.items():
        if metric in actual_kpi and metric in golden:
            delta = abs(float(actual_kpi[metric]) - float(golden[metric]))
            details.append({"metric": metric, "delta": delta, "limit": limit})
            ok = ok and (delta <= limit)

    for metric in categorical_metrics:
        if metric in actual_kpi and metric in golden:
            actual_value = actual_kpi[metric]
            expected_value = golden[metric]
            match = actual_value == expected_value
            details.append({"metric": metric, "actual": actual_value, "expected": expected_value, "match": match})
            ok = ok and match

    return {"golden_available": True, "pass": ok, "details": details}


def read_video_info(feature_dir: Path) -> Dict[str, Any]:
    frame_count = 0
    frame_count_path = feature_dir / "frame_count.txt"
    if frame_count_path.exists():
        try:
            frame_count = int(frame_count_path.read_text(encoding="utf-8", errors="ignore").strip())
        except ValueError:
            frame_count = 0
    else:
        frame_count = len(list(feature_dir.glob("screen_shot_*.tga")))

    mp4_path = feature_dir / "result.mp4"
    mp4_available = mp4_path.exists()

    gtsim_exit_code = None
    gtsim_exit_path = feature_dir / "gtsim_exit_code.txt"
    if gtsim_exit_path.exists():
        try:
            gtsim_exit_code = int(gtsim_exit_path.read_text(encoding="utf-8", errors="ignore").strip())
        except ValueError:
            gtsim_exit_code = None

    video_error = ""
    video_error_path = feature_dir / "video_error.txt"
    if video_error_path.exists():
        video_error = video_error_path.read_text(encoding="utf-8", errors="ignore").strip()

    return {
        "generation_method": "gtsim_direct",
        "frame_count": frame_count,
        "mp4_available": mp4_available,
        "mp4_path": "result.mp4" if mp4_available else "",
        "gtsim_exit_code": gtsim_exit_code,
        "video_error": video_error,
    }


def read_driverscript_info(feature_dir: Path) -> Dict[str, Any]:
    exit_code_raw = ""
    exit_code_path = feature_dir / "driverscript_exit_code.txt"
    if exit_code_path.exists():
        exit_code_raw = exit_code_path.read_text(encoding="utf-8", errors="ignore").strip()

    stdout_path = feature_dir / "python_stdout.txt"
    stderr_path = feature_dir / "python_stderr.txt"
    stdout_exists = stdout_path.exists()
    stderr_exists = stderr_path.exists()

    return {
        "enabled": stdout_exists or stderr_exists or bool(exit_code_raw),
        "exit_code": exit_code_raw,
        "stdout_path": "python_stdout.txt" if stdout_exists else "",
        "stderr_path": "python_stderr.txt" if stderr_exists else "",
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--matrix", required=True)
    parser.add_argument("--thresholds", required=True)
    parser.add_argument("--run-dir", required=True)
    parser.add_argument("--golden-root", default="golden/realdriver_features")
    parser.add_argument("--update-golden", action="store_true")
    args = parser.parse_args()

    matrix = load_yaml(Path(args.matrix))
    threshold_config = load_yaml(Path(args.thresholds))
    thresholds = threshold_config.get("defaults", {})
    baseline_kpi_checks = threshold_config.get("baseline_kpi_checks", [])
    run_dir = Path(args.run_dir)
    golden_root = Path(args.golden_root)
    video_config_path = run_dir / "video_config.json"
    video_config: Dict[str, Any] = {}
    if video_config_path.exists():
        try:
            video_config = json.loads(video_config_path.read_text(encoding="utf-8"))
        except json.JSONDecodeError:
            video_config = {}

    results = []
    overall_pass = True

    for feat in matrix.get("features", []):
        fid = feat["id"]
        fdir = run_dir / fid
        stdout_path = fdir / "stdout.txt"
        text = stdout_path.read_text(encoding="utf-8", errors="ignore") if stdout_path.exists() else ""

        missing_required = []
        for pat in feat.get("required_patterns", []):
            if re.search(pat, text, re.MULTILINE) is None:
                missing_required.append(pat)

        hit_forbidden = []
        for pat in feat.get("forbidden_patterns", []):
            if re.search(pat, text, re.MULTILINE) is not None:
                hit_forbidden.append(pat)

        kpi = compute_kpis(fdir)
        f02_light_eval = evaluate_f02_light_mapping(fid, fdir)
        if f02_light_eval.get("kpi"):
            kpi.update(f02_light_eval["kpi"])
        f03_autolight_eval = evaluate_f03_autolight(fid, fdir)
        if f03_autolight_eval.get("kpi"):
            kpi.update(f03_autolight_eval["kpi"])
        explicit_feature_checks = list(feat.get("kpi_checks", []))
        feature_kpi_checks = list(baseline_kpi_checks) + explicit_feature_checks
        kpi_eval = evaluate_kpi_checks(kpi, feature_kpi_checks)
        feature_kpi_checks_any = list(feat.get("kpi_checks_any", []))
        kpi_any_eval = evaluate_kpi_checks_any(kpi, feature_kpi_checks_any)
        trace_eval = evaluate_trace_integrity(fid, fdir)
        expected_behavior_nl = list(feat.get("expected_behavior_nl", [])) or list(feat.get("validation_points", []))
        judgement_criteria_nl = list(feat.get("judgement_criteria_nl", [])) or build_judgement_criteria_nl(
            feat=feat,
            and_checks=feature_kpi_checks,
            any_checks=feature_kpi_checks_any,
        )
        golden_file = golden_root / fid / "kpi_reference.json"
        golden_cmp = compare_with_golden(kpi, golden_file, thresholds)

        logs_ok = len(missing_required) == 0
        forbidden_ok = len(hit_forbidden) == 0
        kpi_definition_ok = len(explicit_feature_checks) > 0
        trace_ok = trace_eval.get("pass", True)
        light_mapping_ok = f02_light_eval.get("pass", True)
        autolight_ok = f03_autolight_eval.get("pass", True)
        passed = (
            logs_ok
            and forbidden_ok
            and kpi_definition_ok
            and trace_ok
            and light_mapping_ok
            and autolight_ok
            and golden_cmp["pass"]
            and kpi_eval["pass"]
            and kpi_any_eval["pass"]
        )
        overall_pass = overall_pass and passed

        failure_reasons: List[str] = []
        if not logs_ok:
            failure_reasons.append("missing_required_patterns")
        if not forbidden_ok:
            failure_reasons.append("hit_forbidden_patterns")
        if not kpi_definition_ok:
            failure_reasons.append("kpi_checks_empty")
        if not trace_ok:
            failure_reasons.append("trace_integrity_failed")
            failure_reasons.extend(trace_eval.get("mismatch_samples", []))
        if not light_mapping_ok:
            failure_reasons.append("light_mapping_integrity_failed")
            failure_reasons.extend(f02_light_eval.get("mismatch_samples", []))
        if not autolight_ok:
            failure_reasons.append("autolight_integrity_failed")
            failure_reasons.extend(f03_autolight_eval.get("mismatch_samples", []))
        if not kpi_eval["pass"]:
            failure_reasons.append("kpi_checks_failed")
        if not kpi_any_eval["pass"]:
            failure_reasons.append("kpi_checks_any_failed")
        if not golden_cmp["pass"]:
            failure_reasons.append("golden_comparison_failed")

        road_kpi = {
            "lane_id_start": kpi.get("lane_id_start"),
            "lane_id_end": kpi.get("lane_id_end"),
            "lane_change_count": kpi.get("lane_change_count"),
            "s_start_m": kpi.get("s_start_m"),
            "s_end_m": kpi.get("s_end_m"),
            "s_progress_m": kpi.get("s_progress_m"),
            "t_min_m": kpi.get("t_min_m"),
            "t_max_m": kpi.get("t_max_m"),
            "t_abs_max_m": kpi.get("t_abs_max_m"),
            "road_id_mode": kpi.get("road_id_mode"),
        }

        road_golden_details = [
            d for d in golden_cmp.get("details", [])
            if d.get("metric") in {"s_end_m", "s_progress_m", "t_abs_max_m", "lane_id_end", "lane_change_count", "road_id_mode"}
        ]
        road_golden = {
            "golden_available": golden_cmp.get("golden_available", False),
            "pass": golden_cmp.get("pass", True),
            "details": road_golden_details,
        }

        feat_result = {
            "id": fid,
            "name": feat.get("name", ""),
            "scenario": feat.get("scenario", ""),
            "pass": passed,
            "missing_required_patterns": missing_required,
            "hit_forbidden_patterns": hit_forbidden,
            "kpi_checks": kpi_eval,
            "kpi_checks_any": kpi_any_eval,
            "trace_integrity": trace_eval.get("pass", True),
            "trace_stats": trace_eval.get("stats", {}),
            "trace_mismatch_samples": trace_eval.get("mismatch_samples", []),
            "light_mapping_integrity": f02_light_eval.get("pass", True),
            "light_mapping_stats": f02_light_eval.get("stats", {}),
            "light_mapping_mismatch_samples": f02_light_eval.get("mismatch_samples", []),
            "autolight_integrity": f03_autolight_eval.get("pass", True),
            "autolight_stats": f03_autolight_eval.get("stats", {}),
            "autolight_mismatch_samples": f03_autolight_eval.get("mismatch_samples", []),
            "kpi": kpi,
            "golden": golden_cmp,
            "road_kpi": road_kpi,
            "road_golden": road_golden,
            "required_modules_cpp": feat.get("required_modules_cpp", []),
            "required_modules_py": feat.get("required_modules_py", []),
            "validation_points": feat.get("validation_points", []),
            "validation_goal": feat.get("validation_goal", ""),
            "expected_behavior_nl": expected_behavior_nl,
            "judgement_criteria_nl": judgement_criteria_nl,
            "video": read_video_info(fdir),
            "driverscript": read_driverscript_info(fdir),
            "failure_reasons": failure_reasons,
        }

        if args.update_golden:
            target_dir = golden_root / fid
            target_dir.mkdir(parents=True, exist_ok=True)
            with (target_dir / "kpi_reference.json").open("w", encoding="utf-8") as f:
                json.dump(kpi, f, indent=2)

        results.append(feat_result)

    summary = {
        "overall_pass": overall_pass,
        "run_dir": str(run_dir),
        "feature_count": len(results),
        "passed_count": sum(1 for r in results if r["pass"]),
        "video_config": video_config,
        "results": results,
    }

    out = run_dir / "summary.json"
    out.write_text(json.dumps(summary, indent=2), encoding="utf-8")

    print(json.dumps({"overall_pass": overall_pass, "summary": str(out)}, indent=2))
    return 0 if overall_pass else 2


if __name__ == "__main__":
    raise SystemExit(main())
