"""F03 dedicated controller: simple longitudinal tracking with AutoLight ownership."""

from __future__ import annotations

from typing import Any, Dict


class EmbeddedController:
    def __init__(self) -> None:
        self.dt = 0.01

    def init(self, config: Dict[str, Any]) -> None:
        self.dt = float(config.get("dt", 0.01) or 0.01)

    def step(self, frame_data: Dict[str, Any]) -> Dict[str, Any]:
        set_speed = float(frame_data.get("set_speed", 0.0) or 0.0)
        current_speed = float(frame_data.get("current_speed", 0.0) or 0.0)

        gear = -1 if set_speed < -0.05 else 1
        target_abs = abs(set_speed)
        current_abs = abs(current_speed)
        speed_err = target_abs - current_abs

        throttle = 0.0
        brake = 0.0
        if speed_err > 0.1:
            throttle = min(1.0, 0.12 + 0.22 * speed_err)
        elif speed_err < -0.1:
            brake = min(1.0, 0.20 * abs(speed_err))

        return {
            "throttle": float(throttle),
            "brake": float(brake),
            "steering": 0.0,
            "gear": int(gear),
            "lights": {},
            "engine_brake": 0.49,
            "adas_states": [],
        }

    def close(self) -> None:
        return None
