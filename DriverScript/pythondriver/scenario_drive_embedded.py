"""Embedded scenario-drive controller for PythonDriverController."""

from __future__ import annotations

import json
import os
from typing import Any, Dict, List, Optional

from pythondriver.adapters import FrameAdapter, OSIAdapter
from pythondriver.controller_base import EmbeddedControllerBase
from pythondriver.lights import LightState
from realdriver.acc_controller import ACCController
from realdriver.scenario_drive import ScenarioDriveController


class ScenarioDriveEmbedded(EmbeddedControllerBase):
    """ScenarioDrive + ACC composition for embedded runtime."""

    def __init__(self) -> None:
        self.ego_id = 0
        self.dt = 0.01
        self._frame_count = 0
        self._adas_states: List[int] = []
        self._trace_enabled = False
        self._trace_file = None
        self._lights = LightState()
        self._scenario: Optional[ScenarioDriveController] = None
        self._acc: Optional[ACCController] = None
        self._last_wp_generation_version: Optional[int] = None
        self._last_waypoint_index: int = 0

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

        bin_dir = os.path.normpath(os.path.join(script_dir, "..", "..", "bin"))
        lib_path = os.path.join(bin_dir, "esminiRMLib.dll")
        gt_lib_path = os.path.join(bin_dir, "GT_esminiLib.dll")
        if not os.path.exists(gt_lib_path):
            gt_lib_path = None

        self._scenario = ScenarioDriveController(
            lib_path=lib_path,
            xodr_path=xodr_path,
            ego_id=self.ego_id,
            gt_lib_path=gt_lib_path,
            mode="embedded",
        )
        self._acc = ACCController(ego_id=self.ego_id, rm_lib=self._scenario.rm_lib)

    def _resolve_target_speed(self, lon_profile: List[Dict[str, float]], fallback: float) -> float:
        if lon_profile:
            # Use the final profile point (= C++ smoothed target speed).
            # LonProfilePlanner is stateful and ramps smoothed_target_ using
            # SpeedAction TransitionDynamics, so profile[-1] is already smooth.
            return float(lon_profile[-1].get("v_target", fallback))
        return float(fallback)

    def _effective_waypoint_index(self, frame_data: Dict[str, Any]) -> int:
        incoming = int(frame_data.get("waypoint_index", 0))
        waypoint_count = len(frame_data.get("waypoints", []) or [])
        generation = frame_data.get("waypoint_generation", {}) or {}
        version = int(generation.get("version", -1))
        if version == self._last_wp_generation_version:
            return max(self._last_waypoint_index, incoming)
        if waypoint_count > 0 and self._last_waypoint_index > 0 and incoming <= 0:
            clamped_prev = min(self._last_waypoint_index, waypoint_count - 1)
            return max(clamped_prev, incoming)
        return incoming

    @staticmethod
    def _route_endpoint_summary(waypoints: List[Dict[str, Any]]) -> str:
        if not waypoints:
            return "none"
        first = waypoints[0]
        last = waypoints[-1]
        return (
            f"first=({int(first.get('road_id', -1))},{int(first.get('lane_id', 0))},"
            f"{float(first.get('s', 0.0)):.1f}) "
            f"last=({int(last.get('road_id', -1))},{int(last.get('lane_id', 0))},"
            f"{float(last.get('s', 0.0)):.1f})"
        )

    def step(self, frame_data: Dict[str, Any]) -> Dict[str, Any]:
        if self._scenario is None or self._acc is None:
            raise RuntimeError("ScenarioDriveEmbedded is not initialized")

        frame = FrameAdapter.from_dict(frame_data)
        gt = OSIAdapter.parse_ground_truth(frame.ground_truth_bytes)
        # Use the dt from the frame as-is.  When C++ sends dt=0 for the
        # initialisation frame, ScenarioDriveController.update() will load
        # waypoints/target-speed but skip the PID computation, avoiding a
        # derivative kick caused by stale OSI velocity in the first frame.
        dt = frame.dt

        effective_frame_data = dict(frame_data)
        effective_index = self._effective_waypoint_index(frame_data)
        effective_frame_data["waypoint_index"] = effective_index
        generation = frame_data.get("waypoint_generation", {}) or {}
        generation_version = int(generation.get("version", -1))
        if generation_version != self._last_wp_generation_version:
            waypoints = frame_data.get("waypoints", []) or []
            incoming_index = int(frame_data.get("waypoint_index", 0))
            print(
                "[INFO] Embedded: route refresh "
                f"generation={generation_version} incoming_index={incoming_index} "
                f"chosen_index={effective_index} route_len={len(waypoints)} "
                f"{self._route_endpoint_summary(waypoints)}"
            )
        self._scenario._embedded_frame_data = effective_frame_data

        actions = frame_data.get("actions", {}) or {}
        use_acc = bool(actions.get("longitudinal_distance")) or bool(actions.get("synchronize"))

        target_speed = self._resolve_target_speed(frame.lon_profile, frame.set_speed)
        self._scenario.set_target_speed(target_speed)
        self._acc.set_target_speed(max(0.0, target_speed))

        steering = 0.0
        throttle = 0.0
        brake = 0.0
        if gt is not None:
            s_steering, s_throttle, s_brake = self._scenario.update(gt, dt)
            if s_steering is not None:
                # ScenarioDrive steering sign is opposite to embedded vehicle input convention.
                steering = -float(s_steering)
            if use_acc:
                acc_out = self._acc.update(gt, dt)
                throttle = float(acc_out.throttle)
                brake = float(acc_out.brake)
            else:
                throttle = float(s_throttle or 0.0)
                brake = float(s_brake or 0.0)

        if self._scenario.waypoint_mgr is not None:
            self._last_waypoint_index = int(self._scenario.waypoint_mgr.current_index)
        else:
            self._last_waypoint_index = int(effective_frame_data.get("waypoint_index", 0))
        self._last_wp_generation_version = generation_version

        self._frame_count += 1
        gear = -1 if target_speed < -0.05 else 1
        lights_patch = self._lights.to_patch_dict()
        result = FrameAdapter.to_result(
            throttle=throttle,
            brake=brake,
            steering=steering,
            gear=gear,
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
                            "waypoint_index": int(frame_data.get("waypoint_index", 0)),
                            "waypoint_index_effective": int(effective_frame_data.get("waypoint_index", 0)),
                            "waypoint_generation": self._last_wp_generation_version,
                            "lon_profile_count": len(frame.lon_profile),
                            "set_speed": frame.set_speed,
                            "current_speed": frame.current_speed,
                            "dt": frame.dt,
                            "actions": actions,
                        },
                        "control_mode": "acc" if use_acc else "scenario_longitudinal",
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
        if self._scenario is not None:
            self._scenario.close()
        self._scenario = None
        self._acc = None
        return None


class EmbeddedController(ScenarioDriveEmbedded):
    """Default class name expected by xosc (`PythonClass=EmbeddedController`)."""
