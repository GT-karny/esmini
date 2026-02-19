"""Adapter for frame_data passed by PythonDriverBridge."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any, Dict, List

from realdriver.waypoint import Waypoint


@dataclass
class EmbeddedFrame:
    frame_id: int
    ground_truth_bytes: bytes
    waypoints: List[Waypoint]
    waypoint_index: int
    lon_profile: List[Dict[str, float]]
    set_speed: float
    current_speed: float
    dt: float


class FrameAdapter:
    @staticmethod
    def from_dict(frame_data: Dict[str, Any]) -> EmbeddedFrame:
        wp_data = frame_data.get("waypoints", []) or []
        waypoints = [
            Waypoint(
                x=float(w.get("x", 0.0)),
                y=float(w.get("y", 0.0)),
                h=float(w.get("h", 0.0)),
                road_id=int(w.get("road_id", -1)),
                s=float(w.get("s", 0.0)),
                lane_id=int(w.get("lane_id", 0)),
                lane_offset=float(w.get("lane_offset", 0.0)),
            )
            for w in wp_data
        ]

        return EmbeddedFrame(
            frame_id=int(frame_data.get("frame_id", -1)),
            ground_truth_bytes=frame_data.get("ground_truth_bytes") or b"",
            waypoints=waypoints,
            waypoint_index=int(frame_data.get("waypoint_index", 0)),
            lon_profile=frame_data.get("lon_profile", []) or [],
            set_speed=float(frame_data.get("set_speed", 0.0)),
            current_speed=float(frame_data.get("current_speed", 0.0)),
            dt=float(frame_data.get("dt", 0.0)),
        )

    @staticmethod
    def to_result(
        throttle: float,
        brake: float,
        steering: float,
        gear: int = 1,
        lights: Dict[str, str] | None = None,
        engine_brake: float = 0.49,
        adas_states: List[int] | None = None,
    ) -> Dict[str, Any]:
        return {
            "throttle": float(throttle),
            "brake": float(brake),
            "steering": float(steering),
            "gear": int(gear),
            "lights": dict(lights or {}),
            "engine_brake": float(engine_brake),
            "adas_states": list(adas_states or []),
        }
