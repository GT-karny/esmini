#!/usr/bin/env python3
import argparse
import json
import re
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
    speeds = [_parse_float(r.get("speed")) for r in ego_rows]
    times = [t for t in times if t is not None]
    speeds = [s for s in speeds if s is not None]

    if times:
        k["duration_s"] = max(times)
    if speeds:
        k["speed_min_mps"] = min(speeds)
        k["speed_max_mps"] = max(speeds)

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
        k["t_abs_max_m"] = max(abs(v) for v in t_values)

    road_ids = [_parse_int(r.get("roadId")) for r in ego_rows]
    road_ids = [rid for rid in road_ids if rid is not None]
    if road_ids:
        k["road_id_mode"] = Counter(road_ids).most_common(1)[0][0]

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
        prev_speed = _parse_float(prev_row.get("speed"))

        if None in (prev_t, t, prev_s, s, prev_x, x, prev_y, y, prev_speed):
            continue

        dt = max(0.0, t - prev_t)
        duration += dt
        dxy = math.hypot(x - prev_x, y - prev_y)
        xy_path_length += dxy
        if abs(s - prev_s) < s_stall_eps_m and dxy > xy_move_eps_m and abs(prev_speed) > speed_gate_mps:
            s_stall_time += dt

    if duration > 0.0:
        k["s_stall_time_s"] = s_stall_time
        k["s_stall_ratio"] = s_stall_time / duration
    k["xy_path_length_m"] = xy_path_length

    if speeds:
        k["speed_span_mps"] = max(speeds) - min(speeds)

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
            k["lead_gap_min_m"] = min(gaps)
            k["lead_gap_max_m"] = max(gaps)
            k["lead_gap_end_m"] = gaps[-1]
            k["lead_gap_mean_m"] = sum(gaps) / len(gaps)

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
        details.append(detail)
        all_ok = all_ok and ok

    return {"pass": all_ok, "details": details}


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
        feature_kpi_checks = list(baseline_kpi_checks) + list(feat.get("kpi_checks", []))
        kpi_eval = evaluate_kpi_checks(kpi, feature_kpi_checks)
        golden_file = golden_root / fid / "kpi_reference.json"
        golden_cmp = compare_with_golden(kpi, golden_file, thresholds)

        # KPI is the primary verdict axis; required log patterns remain advisory
        # when explicit KPI checks are configured.
        require_log_patterns = len(feature_kpi_checks) == 0
        logs_ok = (len(missing_required) == 0) if require_log_patterns else True
        passed = (logs_ok and len(hit_forbidden) == 0 and golden_cmp["pass"] and kpi_eval["pass"])
        overall_pass = overall_pass and passed

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
            "kpi": kpi,
            "golden": golden_cmp,
            "road_kpi": road_kpi,
            "road_golden": road_golden,
            "required_modules_cpp": feat.get("required_modules_cpp", []),
            "required_modules_py": feat.get("required_modules_py", []),
            "validation_points": feat.get("validation_points", []),
            "video": read_video_info(fdir),
            "driverscript": read_driverscript_info(fdir),
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
        "results": results,
    }

    out = run_dir / "summary.json"
    out.write_text(json.dumps(summary, indent=2), encoding="utf-8")

    print(json.dumps({"overall_pass": overall_pass, "summary": str(out)}, indent=2))
    return 0 if overall_pass else 2


if __name__ == "__main__":
    raise SystemExit(main())
