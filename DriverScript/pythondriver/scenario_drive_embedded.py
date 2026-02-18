"""Embedded scenario-drive controller for PythonDriverController."""

from __future__ import annotations

import os
from typing import Any, Dict, List

from .adapters import FrameAdapter, OSIAdapter
from .controller_base import EmbeddedControllerBase
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

    def init(self, config: Dict[str, Any]) -> None:
        self.dt = float(config.get("dt", 0.01))
        self.ego_id = int(config.get("ego_id", 0))
        script_dir = config.get("script_dir", "")
        xodr_path = config.get("xodr_path", "")

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
        return FrameAdapter.to_result(
            throttle=throttle,
            brake=brake,
            steering=steering,
            gear=1,
            light_mask=0,
            engine_brake=0.49,
            adas_states=self._adas_states,
        )

    def close(self) -> None:
        return None


class EmbeddedController(ScenarioDriveEmbedded):
    """Default class name expected by xosc (`PythonClass=EmbeddedController`)."""

