"""Light state patch helper for PythonDriverController.

Each light can be controlled as:
- "on": force ON (manual override)
- "off": force OFF (manual override)
- "auto": release manual override and delegate to AutoLight

`to_patch_dict()` returns only changed keys since last `clear_patch()`.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Dict, Set


_LIGHT_KEYS = (
    "low_beam",
    "high_beam",
    "left_indicator",
    "right_indicator",
    "fog",
    "brake",
    "reverse",
)


@dataclass
class LightState:
    _values: Dict[str, str] = field(default_factory=dict)
    _dirty: Set[str] = field(default_factory=set)

    def __post_init__(self) -> None:
        for key in _LIGHT_KEYS:
            self._values.setdefault(key, "auto")

    def _set(self, key: str, value: str) -> None:
        if key not in self._values:
            raise KeyError(f"unknown light key: {key}")
        if value not in ("auto", "on", "off"):
            raise ValueError(f"invalid light value: {value}")
        if self._values[key] != value:
            self._values[key] = value
            self._dirty.add(key)

    def _set_bool(self, key: str, on: bool) -> None:
        self._set(key, "on" if on else "off")

    def set_low_beam(self, on: bool) -> None:
        self._set_bool("low_beam", on)

    def set_low_beam_auto(self) -> None:
        self._set("low_beam", "auto")

    def set_high_beam(self, on: bool) -> None:
        self._set_bool("high_beam", on)

    def set_high_beam_auto(self) -> None:
        self._set("high_beam", "auto")

    def set_left_indicator(self, on: bool) -> None:
        self._set_bool("left_indicator", on)

    def set_left_indicator_auto(self) -> None:
        self._set("left_indicator", "auto")

    def set_right_indicator(self, on: bool) -> None:
        self._set_bool("right_indicator", on)

    def set_right_indicator_auto(self) -> None:
        self._set("right_indicator", "auto")

    def set_warning(self, on: bool) -> None:
        self.set_left_indicator(on)
        self.set_right_indicator(on)

    def set_fog(self, on: bool) -> None:
        self._set_bool("fog", on)

    def set_fog_auto(self) -> None:
        self._set("fog", "auto")

    def set_brake(self, on: bool) -> None:
        self._set_bool("brake", on)

    def set_brake_auto(self) -> None:
        self._set("brake", "auto")

    def set_reverse(self, on: bool) -> None:
        self._set_bool("reverse", on)

    def set_reverse_auto(self) -> None:
        self._set("reverse", "auto")

    def to_patch_dict(self) -> Dict[str, str]:
        return {k: self._values[k] for k in self._dirty}

    def clear_patch(self) -> None:
        self._dirty.clear()
