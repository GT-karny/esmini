#!/usr/bin/env python3
import argparse
import csv
import json
import socket
import struct
import sys
import time
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple


def _enum_name(enum_type: Any, value: int) -> Optional[str]:
    try:
        return str(enum_type.Name(value))
    except Exception:
        return None


def _is_brake_on(ls: Any) -> bool:
    value = int(getattr(ls, "brake_light_state", 0))
    enum_type = getattr(ls, "BrakeLightState", None)
    name = _enum_name(enum_type, value) if enum_type is not None else None
    if name is not None:
        return ("OFF" not in name) and ("UNKNOWN" not in name) and ("OTHER" not in name)
    return value not in (0, 1, 2)


def _is_generic_on(value: int, enum_type: Any) -> bool:
    name = _enum_name(enum_type, value) if enum_type is not None else None
    if name is not None:
        return ("OFF" not in name) and ("UNKNOWN" not in name) and ("OTHER" not in name)
    return value not in (0, 1, 2)


def _indicator_state(value: int, enum_type: Any) -> str:
    name = _enum_name(enum_type, value) if enum_type is not None else None
    if name is None:
        return "UNKNOWN"
    if "LEFT" in name:
        return "LEFT"
    if "RIGHT" in name:
        return "RIGHT"
    if "WARNING" in name:
        return "WARNING"
    if "OFF" in name:
        return "OFF"
    return "UNKNOWN"


def _extract_timestamp_s(msg: Any) -> Optional[float]:
    ts = getattr(msg, "timestamp", None)
    if ts is None:
        return None
    sec = getattr(ts, "seconds", None)
    nsec = getattr(ts, "nanos", None)
    if sec is None or nsec is None:
        return None
    return float(sec) + float(nsec) * 1e-9


class OsiLightMetricsCollector:
    def __init__(self):
        self._last_ts: Optional[float] = None
        self._observed_duration_s = 0.0
        self._brake_on_duration_s = 0.0
        self._reverse_on_duration_s = 0.0
        self._observed_frames = 0
        self._brake_on_frames = 0
        self._reverse_on_frames = 0
        self._head_on_frames = 0
        self._high_beam_on_frames = 0
        self._fog_on_frames = 0
        self._indicator_off_frames = 0
        self._indicator_left_frames = 0
        self._indicator_right_frames = 0
        self._indicator_warning_frames = 0
        self._light_state_observed = False
        self._host_vehicle_id_seen = False
        self._host_vehicle_id_last: Optional[int] = None
        self._target_vehicle_id_fallback: Optional[int] = None
        self._host_vehicle_match_frames = 0
        self._unmatched_frames = 0
        self._moving_object_ids_seen: List[int] = []
        self._rows = []

    def observe(self, msg: Any) -> None:
        ts = _extract_timestamp_s(msg)
        dt = 0.0
        if ts is not None and self._last_ts is not None and ts > self._last_ts:
            dt = ts - self._last_ts
        if ts is not None:
            self._last_ts = ts

        has_host_id = hasattr(msg, "HasField") and msg.HasField("host_vehicle_id")
        host_vehicle_id = -1
        if has_host_id:
            host_vehicle_id = int(getattr(getattr(msg, "host_vehicle_id", None), "value", -1))
            self._host_vehicle_id_seen = True
            self._host_vehicle_id_last = host_vehicle_id

        for obj in getattr(msg, "moving_object", []):
            obj_id = getattr(getattr(obj, "id", None), "value", None)
            if obj_id is None:
                continue
            obj_id_int = int(obj_id)
            if obj_id_int not in self._moving_object_ids_seen:
                self._moving_object_ids_seen.append(obj_id_int)

        if host_vehicle_id < 0:
            if self._target_vehicle_id_fallback is not None:
                host_vehicle_id = self._target_vehicle_id_fallback
            else:
                moving_objects = list(getattr(msg, "moving_object", []))
                if moving_objects:
                    first_id = getattr(getattr(moving_objects[0], "id", None), "value", None)
                    if first_id is not None:
                        host_vehicle_id = int(first_id)
                        self._target_vehicle_id_fallback = host_vehicle_id

        target_obj = None
        for obj in getattr(msg, "moving_object", []):
            obj_id = getattr(getattr(obj, "id", None), "value", None)
            if obj_id is not None and int(obj_id) == host_vehicle_id:
                target_obj = obj
                break
        if target_obj is None:
            self._unmatched_frames += 1
            return

        self._host_vehicle_match_frames += 1
        has_vc = hasattr(target_obj, "HasField") and target_obj.HasField("vehicle_classification")
        if not has_vc:
            return
        vc = target_obj.vehicle_classification
        has_ls = hasattr(vc, "HasField") and vc.HasField("light_state")
        if not has_ls:
            return

        self._light_state_observed = True
        ls = vc.light_state
        brake_on = _is_brake_on(ls)
        generic_enum = getattr(ls, "GenericLightState", None)
        indicator_enum = getattr(ls, "IndicatorState", None)
        indicator_raw = int(getattr(ls, "indicator_state", 0))
        indicator_state = _indicator_state(indicator_raw, indicator_enum)
        reverse_raw = int(getattr(ls, "reversing_light", 0))
        reverse_on = _is_generic_on(reverse_raw, generic_enum)
        head_raw = int(getattr(ls, "head_light", 0))
        head_on = _is_generic_on(head_raw, generic_enum)
        high_raw = int(getattr(ls, "high_beam", 0))
        high_on = _is_generic_on(high_raw, generic_enum)
        front_fog_raw = int(getattr(ls, "front_fog_light", 0))
        rear_fog_raw = int(getattr(ls, "rear_fog_light", 0))
        fog_on = _is_generic_on(front_fog_raw, generic_enum) or _is_generic_on(rear_fog_raw, generic_enum)

        self._observed_frames += 1
        if brake_on:
            self._brake_on_frames += 1
        if reverse_on:
            self._reverse_on_frames += 1
        if head_on:
            self._head_on_frames += 1
        if high_on:
            self._high_beam_on_frames += 1
        if fog_on:
            self._fog_on_frames += 1
        if indicator_state == "OFF":
            self._indicator_off_frames += 1
        elif indicator_state == "LEFT":
            self._indicator_left_frames += 1
        elif indicator_state == "RIGHT":
            self._indicator_right_frames += 1
        elif indicator_state == "WARNING":
            self._indicator_warning_frames += 1

        if dt > 0.0:
            self._observed_duration_s += dt
            if brake_on:
                self._brake_on_duration_s += dt
            if reverse_on:
                self._reverse_on_duration_s += dt

        self._rows.append(
            {
                "timestamp_s": ts if ts is not None else -1.0,
                "brake_on": int(brake_on),
                "reverse_on": int(reverse_on),
                "head_light_on": int(head_on),
                "high_beam_on": int(high_on),
                "indicator_state": indicator_state,
                "fog_on": int(fog_on),
                "brake_state_raw": int(getattr(ls, "brake_light_state", 0)),
                "reverse_state_raw": reverse_raw,
                "head_state_raw": head_raw,
                "high_beam_state_raw": high_raw,
                "front_fog_state_raw": front_fog_raw,
                "rear_fog_state_raw": rear_fog_raw,
                "indicator_state_raw": indicator_raw,
            }
        )

    def build_metrics(self) -> Dict[str, Any]:
        if self._observed_duration_s > 0.0:
            brake_ratio = self._brake_on_duration_s / self._observed_duration_s
            reverse_ratio = self._reverse_on_duration_s / self._observed_duration_s
        elif self._observed_frames > 0:
            brake_ratio = float(self._brake_on_frames) / float(self._observed_frames)
            reverse_ratio = float(self._reverse_on_frames) / float(self._observed_frames)
        else:
            brake_ratio = 0.0
            reverse_ratio = 0.0

        return {
            "ego_id": self._host_vehicle_id_last,
            "host_vehicle_id_seen": self._host_vehicle_id_seen,
            "host_vehicle_id_last": self._host_vehicle_id_last,
            "target_vehicle_id_fallback": self._target_vehicle_id_fallback,
            "host_vehicle_match_frames": self._host_vehicle_match_frames,
            "unmatched_frames": self._unmatched_frames,
            "moving_object_ids_seen": self._moving_object_ids_seen,
            "light_state_observed": self._light_state_observed,
            "observed_duration_s": self._observed_duration_s,
            "observed_frames": self._observed_frames,
            "brake_on_ratio": brake_ratio,
            "reverse_on_ratio": reverse_ratio,
            "brake_on_duration_s": self._brake_on_duration_s,
            "reverse_on_duration_s": self._reverse_on_duration_s,
            "brake_on_frames": self._brake_on_frames,
            "reverse_on_frames": self._reverse_on_frames,
            "head_on_frames": self._head_on_frames,
            "high_beam_on_frames": self._high_beam_on_frames,
            "fog_on_frames": self._fog_on_frames,
            "indicator_off_frames": self._indicator_off_frames,
            "indicator_left_frames": self._indicator_left_frames,
            "indicator_right_frames": self._indicator_right_frames,
            "indicator_warning_frames": self._indicator_warning_frames,
        }

    def write_outputs(self, json_out: Path, csv_out: Optional[Path] = None) -> None:
        json_out.parent.mkdir(parents=True, exist_ok=True)
        metrics = self.build_metrics()
        json_out.write_text(json.dumps(metrics, indent=2), encoding="utf-8")

        if csv_out is not None:
            csv_out.parent.mkdir(parents=True, exist_ok=True)
            with csv_out.open("w", encoding="utf-8", newline="") as f:
                writer = csv.DictWriter(
                    f,
                    fieldnames=[
                        "timestamp_s",
                        "head_light_on",
                        "high_beam_on",
                        "indicator_state",
                        "fog_on",
                        "brake_on",
                        "reverse_on",
                        "head_state_raw",
                        "high_beam_state_raw",
                        "indicator_state_raw",
                        "front_fog_state_raw",
                        "rear_fog_state_raw",
                        "brake_state_raw",
                        "reverse_state_raw",
                    ],
                )
                writer.writeheader()
                for row in self._rows:
                    writer.writerow(row)


def _build_ground_truth_message() -> Tuple[Any, socket.socket]:
    repo_root = Path(__file__).resolve().parent.parent
    scripts_dir = repo_root / "scripts"
    if str(scripts_dir) not in sys.path:
        sys.path.insert(0, str(scripts_dir))
    from osi3.osi_groundtruth_pb2 import GroundTruth  # type: ignore
    return GroundTruth, socket.socket(socket.AF_INET, socket.SOCK_DGRAM)


def run_udp_capture(
    host: str,
    port: int,
    timeout_s: float,
    duration_s: float,
    json_out: Path,
    csv_out: Optional[Path],
) -> int:
    GroundTruth, sock = _build_ground_truth_message()
    sock.bind((host, port))
    sock.settimeout(timeout_s)
    collector = OsiLightMetricsCollector()
    start = time.time()
    chunk_buffer = bytearray()
    while (time.time() - start) <= duration_s:
        try:
            data, _ = sock.recvfrom(65536)
        except socket.timeout:
            continue
        if len(data) < 8:
            continue

        try:
            counter, data_size = struct.unpack("<iI", data[:8])
        except struct.error:
            continue

        if data_size > (len(data) - 8):
            continue

        chunk = data[8:8 + data_size]
        idx = abs(counter)
        if idx == 1:
            chunk_buffer = bytearray()
        chunk_buffer.extend(chunk)

        if counter < 0:
            msg = GroundTruth()
            try:
                msg.ParseFromString(bytes(chunk_buffer))
            except Exception:
                chunk_buffer = bytearray()
                continue
            collector.observe(msg)
            chunk_buffer = bytearray()
    sock.close()
    collector.write_outputs(json_out=json_out, csv_out=csv_out)
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Collect OSI brake/reverse light metrics.")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=48198)
    parser.add_argument("--timeout", type=float, default=0.5)
    parser.add_argument("--duration", type=float, default=12.0)
    parser.add_argument("--json-out", required=True)
    parser.add_argument("--csv-out", default="")
    args = parser.parse_args()

    json_out = Path(args.json_out)
    csv_out = Path(args.csv_out) if args.csv_out else None
    return run_udp_capture(
        host=args.host,
        port=args.port,
        timeout_s=args.timeout,
        duration_s=args.duration,
        json_out=json_out,
        csv_out=csv_out,
    )


if __name__ == "__main__":
    raise SystemExit(main())
