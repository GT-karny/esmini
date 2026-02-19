"""F02 dedicated embedded controller for strict light-mask mapping validation."""

from __future__ import annotations

import json
import os
from typing import Any, Dict

from pythondriver.lights import LightState


class EmbeddedController:
    def __init__(self) -> None:
        self.dt = 0.01
        self.trace_enabled = False
        self.trace_file = None
        self.lights = LightState()

    def init(self, config: Dict[str, Any]) -> None:
        self.dt = float(config.get("dt", 0.01))
        self.trace_enabled = bool(config.get("trace_enabled", False))
        trace_dir = str(config.get("trace_dir", "") or "").strip()
        if self.trace_enabled:
            out_dir = trace_dir or "."
            os.makedirs(out_dir, exist_ok=True)
            self.trace_file = open(os.path.join(out_dir, "python_trace.jsonl"), "w", encoding="utf-8")

    def _phase(self, sim_t: float) -> str:
        if sim_t < 0.5:
            return "OFF0"
        if sim_t < 1.5:
            return "LOW"
        if sim_t < 2.5:
            return "HIGH"
        if sim_t < 3.5:
            return "LEFT"
        if sim_t < 4.5:
            return "RIGHT"
        if sim_t < 5.5:
            return "FOG"
        if sim_t < 6.5:
            return "WARNING"
        if sim_t < 7.5:
            return "BRAKE"
        if sim_t < 8.5:
            return "REVERSE"
        return "OFF1"

    def step(self, frame_data: Dict[str, Any]) -> Dict[str, Any]:
        frame_id = int(frame_data.get("frame_id", -1))
        dt = float(frame_data.get("dt", self.dt) or self.dt)
        sim_t = max(0.0, frame_id * max(dt, 1e-3))
        phase = self._phase(sim_t)

        throttle = 0.20
        brake = 0.0
        steering = 0.0
        gear = 1
        self.lights.set_low_beam(False)
        self.lights.set_high_beam(False)
        self.lights.set_left_indicator(False)
        self.lights.set_right_indicator(False)
        self.lights.set_fog(False)
        self.lights.set_brake(False)
        self.lights.set_reverse(False)

        if phase == "LOW":
            self.lights.set_low_beam(True)
        elif phase == "HIGH":
            self.lights.set_high_beam(True)
        elif phase == "LEFT":
            self.lights.set_left_indicator(True)
        elif phase == "RIGHT":
            self.lights.set_right_indicator(True)
        elif phase == "FOG":
            self.lights.set_fog(True)
        elif phase == "WARNING":
            self.lights.set_warning(True)
        elif phase == "BRAKE":
            throttle = 0.0
            brake = 0.60
            self.lights.set_brake(True)
        elif phase == "REVERSE":
            throttle = 0.20
            brake = 0.0
            gear = -1
            self.lights.set_reverse(True)

        lights_patch = self.lights.to_patch_dict()
        result = {
            "throttle": throttle,
            "brake": brake,
            "steering": steering,
            "gear": gear,
            "lights": lights_patch,
            "engine_brake": 0.49,
            "adas_states": [],
        }
        self.lights.clear_patch()

        if self.trace_enabled and self.trace_file is not None:
            self.trace_file.write(
                json.dumps(
                    {
                        "frame_id": frame_id,
                        "recv": {
                            "dt": dt,
                            "set_speed": float(frame_data.get("set_speed", 0.0)),
                            "current_speed": float(frame_data.get("current_speed", 0.0)),
                            "waypoint_count": len(frame_data.get("waypoints", []) or []),
                            "lon_profile_count": len(frame_data.get("lon_profile", []) or []),
                            "gt_bytes_len": len(frame_data.get("ground_truth_bytes", b"") or b""),
                        },
                        "phase": phase,
                        "send": {
                            "throttle": throttle,
                            "brake": brake,
                            "steering": steering,
                            "gear": gear,
                            "lights": lights_patch,
                            "engine_brake": 0.49,
                        },
                    },
                    ensure_ascii=True,
                )
                + "\n"
            )
        return result

    def close(self) -> None:
        if self.trace_file is not None:
            self.trace_file.close()
            self.trace_file = None
