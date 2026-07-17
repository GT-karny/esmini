"""VirtualDriver (Phase 1-3) runtime configuration API endpoints.

Mirrors ``auto_light_api`` (audit WEB-*, GitHub issue #33): reads/writes the
shared config file ``CONFIG_DIR / virtual_driver.json`` — the same file
``ConfigLoader::ResolveConfigPath`` resolves at runtime and that
``_write_virtual_driver_config()`` (services/simulation_runner.py) reads as its
base for every run before forcing ``input_type=network`` and additively
enabling scenario ``<Property name="policies">`` policies. Editing the shared
file here is therefore automatically picked up on the next run — no runner
change is needed for persistence.

Deliberately EXCLUDED from the known/editable keys: ``input_type``,
``input_port``, ``input_transport``, ``vehicle_params_file`` — the runner
(``_write_virtual_driver_config``) owns those at run time and a GUI edit must
not fight it. GET still returns them (they live in the on-disk file, returned
verbatim); PUT rejects them as unknown (422).
"""

from __future__ import annotations

import json
from typing import Any

from fastapi import APIRouter, HTTPException

from GT_esmini.web.backend.config import CONFIG_DIR

router = APIRouter(prefix="/api/virtual-driver", tags=["virtual-driver"])

VIRTUAL_DRIVER_CONFIG_FILE = "virtual_driver.json"

# Known editable keys, split by expected JSON type. ``bool`` is validated before
# ``number`` because in Python ``bool`` is a subclass of ``int`` — a boolean must
# never satisfy a numeric field, nor vice versa.
_BOOL_KEYS = frozenset({
    "policy_lead_enabled",
    "policy_traffic_light_enabled",
    "policy_stop_yield_enabled",
    "policy_conflict_enabled",
    "policy_crosswalk_enabled",
    "policy_junction_priority_enabled",
    "policy_aeb_enabled",
    "respect_speed_limit",
    "crosswalk_yield_to_waiting",
    "crosswalk_ped_signal_aware",
    "override_enabled",
    "override_button",
})
_NUMBER_KEYS = frozenset({
    "horizon_s",
    "short_dt",
    "max_lateral_accel",
    "comfort_decel",
    "emergency_decel",
    "comfort_jerk",
    "scan_distance",
    "scan_step",
    "turn_speed",
    "min_turn_speed",
    "stop_band",
    "lookahead_gain",
    "min_lookahead",
    "max_lookahead",
    "max_steer_angle",
    "steering_sign",
    "speed_kp",
    "speed_ki",
    "speed_kd",
    "control_point_offset",
    "control_point_min_speed",
    "indicator_lead_time",
    "indicator_min_on_time",
    "idm_time_headway",
    "idm_min_gap",
    "idm_max_accel",
    "idm_comfort_decel",
    "idm_desired_speed",
    "idm_lookahead",
    "idm_lateral_tol",
    "idm_target_horizon",
    "tl_lookahead",
    "tl_yellow_decel",
    "tl_stop_margin",
    "sign_lookahead",
    "stop_hold_time",
    "stop_detect_speed",
    "stop_line_tol",
    "creep_speed",
    "creep_advance",
    "yield_creep_speed",
    "sign_stop_margin",
    "conflict_lookahead",
    "conflict_step",
    "conflict_lane_margin",
    "conflict_standoff",
    "conflict_release_buffer",
    "conflict_pet",
    "conflict_nominal_speed",
    "conflict_min_cross_angle_deg",
    "conflict_other_min_speed",
    "conflict_area_eps",
    "crosswalk_lookahead",
    "crosswalk_step",
    "crosswalk_standoff",
    "crosswalk_wait_margin",
    "crosswalk_signal_link_radius",
    "crosswalk_release_lateral_margin",
    "aeb_ttc_threshold",
    "aeb_lateral_tol",
    "aeb_min_a_req",
    "aeb_stop_margin",
    "steering_threshold",
    "throttle_threshold",
    "brake_threshold",
    "auto_return_timeout",
})
# String enum keys: 'manual' (overridable) or 'scenario' (locked-auto).
_STRING_ENUM_KEYS: dict[str, frozenset[str]] = {
    "override_lateral": frozenset({"manual", "scenario"}),
    "override_longitudinal": frozenset({"manual", "scenario"}),
}
KNOWN_KEYS = _BOOL_KEYS | _NUMBER_KEYS | frozenset(_STRING_ENUM_KEYS)

# Owned by the per-run writer (_write_virtual_driver_config); a GUI edit must
# never override these, so PUT rejects them as unknown.
_EXCLUDED_KEYS = frozenset({
    "input_type",
    "input_port",
    "input_transport",
    "vehicle_params_file",
})

# Shipping defaults — mirror GT_esmini/config/virtual_driver.json, comment
# ("_xxx") keys included so a freshly-written file stays self-documenting.
# Single source of truth for both the GET /config fallback (file absent) and
# GET /defaults (reset).
DEFAULT_VIRTUAL_DRIVER_CONFIG: dict[str, Any] = {
    "_comment": "ControllerVirtualDriver (Phase 1) — full-physics virtual driver. Flat unique keys (line-parsed).",

    "vehicle_params_file": "real_vehicle_params.json",

    "_planner": "Short planner: equal-dt trajectory preview",
    "horizon_s": 3.0,
    "short_dt": 0.1,

    "_midlong": "Phase 2 mid/long planner: v_target(s) ceiling (curvature/junction/speed-limit) shaped by comfort decel. Combined with SpeedAction target via min(). emergency_decel shapes the approach to a SAFETY-tier constraint (AEB) instead of comfort_decel; see PolicyConstraint::Tier.",
    "max_lateral_accel": 2.0,
    "comfort_decel": 2.0,
    "emergency_decel": 8.0,
    "comfort_jerk": 1.5,
    "scan_distance": 300.0,
    "scan_step": 2.0,
    "turn_speed": 5.0,
    "min_turn_speed": 2.0,
    "stop_band": 2.0,
    "respect_speed_limit": True,

    "_driver": "PID + Pure Pursuit driver model",
    "lookahead_gain": 0.5,
    "min_lookahead": 4.0,
    "max_lookahead": 20.0,
    "max_steer_angle": 0.61,
    "steering_sign": -1.0,
    "speed_kp": 0.6,
    "speed_ki": 0.2,
    "speed_kd": 0.0,

    "_control_point": "P2 issue 2: shift the lateral control point + preview anchor forward (rear->front axle) so the front stays in-lane on tight turns. control_point_offset [m]: >0 explicit, 0=auto(wheel_base), <0 disabled. Only above control_point_min_speed and not during a storyboard lane maneuver.",
    "control_point_offset": 0.0,
    "control_point_min_speed": 1.0,

    "_indicator": "Auto turn-signal",
    "indicator_lead_time": 2.0,
    "indicator_min_on_time": 0.3,

    "_policies": "Phase 3 traffic policies. Each emits PolicyConstraints folded into v_target(s) by the mid/long planner (strictest wins; STOP->0). Default OFF so Phase 1/2 behavior is unchanged; opt in per scenario.",
    "policy_lead_enabled": False,
    "policy_traffic_light_enabled": False,
    "policy_stop_yield_enabled": False,
    "policy_conflict_enabled": False,
    "policy_crosswalk_enabled": False,
    "policy_junction_priority_enabled": False,
    "policy_aeb_enabled": False,

    "_policy_lead": "3a lead-vehicle IDM follow",
    "idm_time_headway": 1.5,
    "idm_min_gap": 2.0,
    "idm_max_accel": 1.5,
    "idm_comfort_decel": 2.0,
    "idm_desired_speed": 50.0,
    "idm_lookahead": 120.0,
    "idm_lateral_tol": 2.0,
    "idm_target_horizon": 0.5,

    "_policy_traffic_light": "3b traffic light. Yellow: stop only if dist > yellow_margin * braking distance.",
    "tl_lookahead": 80.0,
    "tl_yellow_decel": 4.0,
    "tl_stop_margin": 3.0,

    "_policy_stop_yield": "3c STOP (dwell+creep FSM) / YIELD (decelerate only; stop deferred to 3d). stop_margin halts before the line so the sign stays in scan (no creep-through).",
    "sign_lookahead": 80.0,
    "stop_hold_time": 1.5,
    "stop_detect_speed": 0.3,
    "stop_line_tol": 2.0,
    "creep_speed": 2.0,
    "creep_advance": 4.0,
    "yield_creep_speed": 3.0,
    "sign_stop_margin": 3.0,

    "_policy_conflict": "3d conflict-corridor resolver. Each vehicle's future motion is a width-inflated path CORRIDOR (ribbon of convex quads, half_width + conflict_lane_margin). The conflict REGION is the TRUE polygon intersection of the ego corridor and an oncoming corridor (Sutherland-Hodgman clip; the cluster nearest the ego). Length-aware constant-speed timing of when each body occupies the region (with conflict_pet post-encroachment pad) decides yield; the ego arrival is floored at conflict_nominal_speed (anti-chatter). On conflict, emits STOP_AT_S conflict_standoff before the region entry. Crawl is allowed (the planner may bottom out ~1-2 m/s); the standoff keeps the ego footprint out of the region. POSITIONAL release: held until the governing oncoming's body has driven past the region exit by conflict_release_buffer. The ego (turning/crossing vehicle) always yields to oncoming through-traffic here; the road RoadRule is read for F3 only (junction priority policy). Default OFF.",
    "conflict_lookahead": 120.0,
    "conflict_step": 1.0,
    "conflict_lane_margin": 0.25,
    "conflict_standoff": 5.0,
    "conflict_release_buffer": 3.0,
    "conflict_pet": 1.5,
    "conflict_nominal_speed": 5.0,
    "conflict_min_cross_angle_deg": 20.0,
    "conflict_other_min_speed": 0.5,
    "conflict_area_eps": 0.10,

    "_policy_crosswalk": "3d ext crosswalk pedestrian yield. Walks the ego route for OpenDRIVE crosswalk objects and yields to pedestrians. Two-layer: CROSSING rule (a ped ON the footprint blocks — unconditional collision avoidance, never signal-gated) + WAITING rule (a ped within crosswalk_wait_margin of the footprint, about to cross — courtesy/law, JP default crosswalk_yield_to_waiting). The waiting rule is gated by a linked pedestrian signal (type 1000002 within crosswalk_signal_link_radius on the same road) when crosswalk_ped_signal_aware: RED ped phase suppresses it (proceed), GREEN/ambiguous keeps it active. Passage band = ego half-width + crosswalk_release_lateral_margin (a ped outside the band moving away does not block). On block, latches and emits STOP_AT_S crosswalk_standoff before the footprint entry; releases when no blocking ped remains. Covers the speed-0 waiting ped that the occupancy-based conflict resolver misses (empty corridor). Default OFF.",
    "crosswalk_lookahead": 80.0,
    "crosswalk_step": 1.0,
    "crosswalk_standoff": 3.0,
    "crosswalk_wait_margin": 2.0,
    "crosswalk_yield_to_waiting": True,
    "crosswalk_ped_signal_aware": True,
    "crosswalk_signal_link_radius": 10.0,
    "crosswalk_release_lateral_margin": 0.5,

    "_policy_junction_priority": "F3 unsignalized-junction priority. Reads OpenDRIVE <priority> (fallback heuristic when absent) to decide right-of-way at uncontrolled junctions; shares the conflict-corridor machinery above. Default OFF.",

    "_policy_aeb": "AEB phase 1 (see AebSafety). Forward-collision emergency-braking guardian, independent of policy_lead_enabled: admits a candidate ahead within aeb_lateral_tol whose |dLaneId|<=1 (not just ==0, so a still-changing-lane cut-in is seen), and requires it be either already same-lane or actively encroaching (lateral offset shrinking toward the ego lane frame-to-frame) before it is even considered. Fires ONE SAFETY-tier STOP_AT_S (ramped by the planner at emergency_decel, not comfort_decel) only when the collision-course gate trips: closing speed v_close>0, bumper gap>0, time-to-collision < aeb_ttc_threshold, and required decel > aeb_min_a_req. Longitudinal-only (no steering). Default OFF.",
    "aeb_ttc_threshold": 2.5,
    "aeb_lateral_tol": 3.5,
    "aeb_min_a_req": 3.0,
    "aeb_stop_margin": 2.0,

    "_override": "Manual override (reuses ManualDrive OverrideManager). mode mapping: never=override_enabled:false, deadzone/mix=thresholds, always=thresholds:0. lateral/longitudinal: 'manual' overridable, 'scenario' locked-auto.",
    "override_enabled": True,
    "override_button": True,
    "steering_threshold": 0.05,
    "throttle_threshold": 0.1,
    "brake_threshold": 0.1,
    "auto_return_timeout": 0.0,
    "override_lateral": "manual",
    "override_longitudinal": "manual",

    "_input": "Input source: stub | network | sdl2_wheel",
    "input_type": "stub",
    "input_port": 9100,
    "input_transport": "udp",
}


def _config_path():
    return CONFIG_DIR / VIRTUAL_DRIVER_CONFIG_FILE


def _read_config() -> dict[str, Any]:
    """Return the on-disk config (comments + excluded keys included), or the
    shipped defaults."""
    path = _config_path()
    if path.exists():
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
            if isinstance(data, dict):
                return data
        except (json.JSONDecodeError, OSError):
            pass
    return dict(DEFAULT_VIRTUAL_DRIVER_CONFIG)


def _coerce(key: str, value: Any) -> Any:
    """Type-check a single known key; return the value to persist.

    bool keys accept only JSON booleans; number keys accept int/float but
    reject booleans; enum string keys accept only their allowed literal set.
    Raises HTTPException(422) on mismatch.
    """
    if key in _BOOL_KEYS:
        if not isinstance(value, bool):
            raise HTTPException(status_code=422, detail=f"'{key}' must be a boolean")
        return value
    if key in _STRING_ENUM_KEYS:
        allowed = _STRING_ENUM_KEYS[key]
        if not isinstance(value, str) or value not in allowed:
            raise HTTPException(
                status_code=422,
                detail=f"'{key}' must be one of {sorted(allowed)}",
            )
        return value
    # number key
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise HTTPException(status_code=422, detail=f"'{key}' must be a number")
    return float(value)


@router.get("/config")
async def get_config() -> dict[str, Any]:
    """Read current virtual_driver.json (VD planner/policy/driver settings).

    Includes the "_..." comment keys and the runner-owned ``input_*`` /
    ``vehicle_params_file`` keys (spec documentation / on-disk truth). Falls
    back to the shipped defaults when the file is absent.
    """
    return _read_config()


@router.get("/defaults")
async def get_defaults() -> dict[str, Any]:
    """Return the factory-default virtual_driver.json values (for the Reset button)."""
    return dict(DEFAULT_VIRTUAL_DRIVER_CONFIG)


@router.put("/config")
async def update_config(patch: dict[str, Any]) -> dict[str, Any]:
    """Write virtual_driver.json.

    - Only known ``policy_*`` / planner / driver / policy-tuning keys are
      accepted; any other non-comment key — including the runner-owned
      ``input_type`` / ``input_port`` / ``input_transport`` /
      ``vehicle_params_file`` — is rejected (422).
    - Each value is type-checked (bool / number / enum string).
    - "_..." comment keys and the runner-owned keys are preserved: we start
      from the existing file (or the shipped defaults) and overwrite only the
      supplied known keys, so the self-documenting comments and the per-run
      writer's inputs survive the round-trip. Incoming comment keys are
      ignored (the server keeps its own), so a client cannot inject arbitrary
      text.
    """
    for key in patch:
        if key.startswith("_"):
            continue
        if key not in KNOWN_KEYS:
            raise HTTPException(status_code=422, detail=f"Unknown key: '{key}'")

    merged = _read_config()
    for key, value in patch.items():
        if key.startswith("_"):
            continue
        merged[key] = _coerce(key, value)

    path = _config_path()
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        json.dumps(merged, indent=4, ensure_ascii=False),
        encoding="utf-8",
    )
    return merged
