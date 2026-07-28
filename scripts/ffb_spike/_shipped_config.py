"""feature:F7 (wheel autocal prep) -- single source of truth for the shipped
FFB target-track / shadow-model constants that several offline analysis
scripts in this directory used to duplicate as hardcoded Python module-level
constants (KINETIC/BRK_HI/BRK_LEFT/BRK_RIGHT/SLOPE/VMAX/...).

Duplicating these values was flagged in
test_results/f7_wheel_autocal_requirements.md sec 6-1/7-5: once wheel
autocalibration exists, config/virtual_driver.json's values change per
device, and any tool with its own copy of "the G29 numbers" silently goes
stale the moment a different wheel is calibrated -- the same failure shape
("looks right on the device it was written for, wrong on any other")
identified elsewhere in this project's audits. Read from config instead of
re-declaring; do not add a third copy anywhere.

Not a general-purpose config loader -- exposes exactly the fields these
regime-classification / decomposition tools need, by the same names the
shipped JSON uses minus the "ffb_target_track_" / "ffb_target_track_override_"
prefixes, so callers can keep their existing short names (KINETIC, BRK_HI,
...) by assigning from the returned object instead of hardcoding.
"""
from __future__ import annotations

import json
import math
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
SHIPPED_CFG = REPO_ROOT / "GT_esmini" / "config" / "virtual_driver.json"


class ShadowConstants:
    __slots__ = (
        "kp", "kd", "max_force", "hard_stop_zone",
        "friction_ff", "friction_ff_eps",
        "kinetic", "brk_hi", "brk_left", "brk_right",
        "slope", "vmax", "residual_threshold",
    )

    def __init__(self, cfg: dict):
        self.kp = cfg["ffb_target_track_kp"]
        self.kd = cfg["ffb_target_track_kd"]
        self.max_force = cfg["ffb_target_track_max_force"]
        self.hard_stop_zone = cfg["ffb_target_track_hard_stop_zone"]
        self.friction_ff = cfg["ffb_target_track_friction_ff"]
        self.friction_ff_eps = cfg["ffb_target_track_friction_ff_eps"]
        self.kinetic = cfg["ffb_target_track_override_shadow_kinetic"]
        self.brk_hi = cfg["ffb_target_track_override_shadow_breakaway"]
        self.brk_left = cfg["ffb_target_track_override_shadow_breakaway_left"]
        self.brk_right = cfg["ffb_target_track_override_shadow_breakaway_right"]
        self.slope = cfg["ffb_target_track_override_shadow_force_to_velocity"]
        self.vmax = cfg["ffb_target_track_override_shadow_v_max"]
        self.residual_threshold = cfg["ffb_target_track_override_residual_threshold"]


def load(path: Path | None = None) -> ShadowConstants:
    """Read config/virtual_driver.json (or an override path, e.g. a per-run
    config carrying a specific device's calibrated values) and return the
    shadow/target-track constants these analysis tools classify regimes
    against. Raises KeyError with the missing field name if the config is
    missing an expected key (fail loud, do not fall back to a hardcoded
    default -- that would silently recreate the duplication this module
    exists to remove)."""
    cfg_path = path or SHIPPED_CFG
    cfg = json.loads(cfg_path.read_text(encoding="utf-8"))
    return ShadowConstants(cfg)


def undetectable_band(sc: ShadowConstants | None = None) -> float:
    """The true value of the direct-axis "undetectable" resistance band: the
    x solving kp*x + friction_ff*tanh(x/friction_ff_eps) = breakaway (see
    ffb_override_tuning.md sec on the detection floor). This is a DERIVED
    quantity, not a config field, wheel_session_report.py used to hardcode
    the shipped-G29 solution (0.01729) as a constant -- solve it fresh here
    so a recalibrated device's own kp/friction_ff/eps/breakaway keep it
    correct (feature:F7 wheel autocal requirements sec 6-1/7-5)."""
    sc = sc or load()
    lo, hi = 0.0, 1.0

    def f(x: float) -> float:
        return sc.kp * x + sc.friction_ff * math.tanh(x / sc.friction_ff_eps) - sc.brk_hi

    for _ in range(100):
        mid = (lo + hi) / 2.0
        if f(mid) > 0:
            hi = mid
        else:
            lo = mid
    return (lo + hi) / 2.0
