"""Embedded scenario-drive controller for PythonDriverController."""

from __future__ import annotations

import json
import os
from typing import Any, Dict, List

from .adapters import FrameAdapter, OSIAdapter
from .controller_base import EmbeddedControllerBase
from .lights import LightState
from realdriver.lateral_controller import LateralController
from realdriver.longitudinal_controller import LongitudinalController
from realdriver.waypoint import Waypoint


class ScenarioDriveEmbedded(EmbeddedControllerBase):
    """Combined lateral + longitudinal controller for embedded runtime."""

    def __init__(self) -> None:
        self.ego_id = 0
        self.dt = 0.01
        self._frame_count = 0
        self._last_waypoint_sig = None
        self._adas_states: List[int] = []
        self.lateral: LateralController | None = None
        self.longitudinal: LongitudinalController | None = None
        self._trace_enabled = False
        self._trace_file = None
        self._lights = LightState()

    def init(self, config: Dict[str, Any]) -> None:
        self.dt = float(config.get("dt", 0.01))
        self.ego_id = int(config.get("ego_id", 0))
        script_dir = config.get("script_dir", "")
        xodr_path = config.get("xodr_path", "")
        self._trace_enabled = bool(config.get("trace_enabled", False))
        trace_dir = str(config.get("trace_dir", "") or "").strip()

        if self._trace_enabled:
            out_dir = trace_dir or "."
            os.makedirs(out_dir, exist_ok=True)
            self._trace_file = open(os.path.join(out_dir, "python_trace.jsonl"), "w", encoding="utf-8")

        self.lateral = LateralController(ego_id=self.ego_id)
        self.longitudinal = LongitudinalController(ego_id=self.ego_id)

        # Optional RoadManager assist.
        try:
            from realdriver.rm_lib import EsminiRMLib

            lib_path = os.path.join(script_dir, "..", "bin", "esminiRMLib.dll")
            if os.path.exists(lib_path) and xodr_path:
                rm_lib = EsminiRMLib(lib_path)
                rm_lib.Init(xodr_path)
                self.lateral = LateralController(rm_lib=rm_lib, ego_id=self.ego_id)
        except Exception:
            # Keep running without RM binding.
            pass

    def _update_waypoints(self, waypoints: List[Waypoint], waypoint_index: int) -> None:
        if self.lateral is None:
            return
        if not waypoints:
            return

        sig = (
            len(waypoints),
            round(waypoints[0].x, 3),
            round(waypoints[0].y, 3),
            round(waypoints[-1].x, 3),
            round(waypoints[-1].y, 3),
        )
        if sig != self._last_waypoint_sig:
            self.lateral.set_calculated_waypoints(waypoints)
            self._last_waypoint_sig = sig
        self.lateral.current_waypoint_index = waypoint_index

    def _resolve_target_speed(self, lon_profile: List[Dict[str, float]], fallback: float) -> float:
        if lon_profile:
            return float(lon_profile[-1].get("v_target", fallback))
        return float(fallback)

    def step(self, frame_data: Dict[str, Any]) -> Dict[str, Any]:
        if self.lateral is None or self.longitudinal is None:
            raise RuntimeError("ScenarioDriveEmbedded is not initialized")

        frame = FrameAdapter.from_dict(frame_data)
        gt = OSIAdapter.parse_ground_truth(frame.ground_truth_bytes)

        self._update_waypoints(frame.waypoints, frame.waypoint_index)
        target_speed = self._resolve_target_speed(frame.lon_profile, frame.set_speed)
        self.longitudinal.set_target_speed(target_speed)

        steering = 0.0
        throttle = 0.0
        brake = 0.0
        if gt is not None:
            steering = self.lateral.update(gt, frame.dt if frame.dt > 0.0 else self.dt)
            lon = self.longitudinal.update(gt, frame.dt if frame.dt > 0.0 else self.dt)
            throttle = lon.throttle
            brake = lon.brake

        self._frame_count += 1
        lights_patch = self._lights.to_patch_dict()
        result = FrameAdapter.to_result(
            throttle=throttle,
            brake=brake,
            steering=steering,
            gear=1,
            lights=lights_patch,
            engine_brake=0.49,
            adas_states=self._adas_states,
        )
        self._lights.clear_patch()
        if self._trace_enabled and self._trace_file is not None:
            self._trace_file.write(
                json.dumps(
                    {
                        "frame_id": frame.frame_id,
                        "recv": {
                            "gt_bytes_len": len(frame.ground_truth_bytes or b""),
                            "waypoint_count": len(frame.waypoints),
                            "lon_profile_count": len(frame.lon_profile),
                            "set_speed": frame.set_speed,
                            "current_speed": frame.current_speed,
                            "dt": frame.dt,
                        },
                        "send": {
                            "throttle": result["throttle"],
                            "brake": result["brake"],
                            "steering": result["steering"],
                            "gear": result["gear"],
                            "lights": result["lights"],
                            "engine_brake": result["engine_brake"],
                        },
                    },
                    ensure_ascii=True,
                )
                + "\n"
            )
        return result

    def close(self) -> None:
        if self._trace_file is not None:
            self._trace_file.close()
            self._trace_file = None
        return None


class EmbeddedController(ScenarioDriveEmbedded):
    """Default class name expected by xosc (`PythonClass=EmbeddedController`)."""
