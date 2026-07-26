"""VirtualDriver (Phase 1-3) runtime configuration API endpoints.

Mirrors ``auto_light_api`` (audit WEB-*, GitHub issue #33): reads/writes the
shared config file ``CONFIG_DIR / virtual_driver.json`` — the same file
``ConfigLoader::ResolveConfigPath`` resolves at runtime and that
``_write_virtual_driver_config()`` (services/simulation_runner.py) reads as its
base for every run, additively enabling scenario
``<Property name="policies">`` policies. Editing the shared file here is
therefore automatically picked up on the next run — no runner change is
needed for persistence.

``input_type`` is editable here (string-enum key; see ``_STRING_ENUM_KEYS``)
so the GUI can choose the run's input source. ``_write_virtual_driver_config``
still defaults a ``"stub"`` (the shipped default, meaning nothing was ever
chosen) up to ``"network"`` so the web override panel (``/ws/input``) keeps
working out of the box, but an explicit ``"network"`` or ``"sdl2_wheel"``
choice made here passes through unmodified — see that function's docstring
for the "sdl2_wheel disables the override panel" caveat.

Deliberately EXCLUDED from the known/editable keys: ``input_port``,
``input_transport``, ``vehicle_params_file`` — the runner
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
_BOOL_KEYS = frozenset(
    {
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
        # feature:F7 (F7b) — FFB target-tracking master gate.
        "ffb_target_track_enabled",
        # feature:F7 — AD steering safety envelope master gate (default ON).
        "ad_steering_envelope_enabled",
    }
)
_NUMBER_KEYS = frozenset(
    {
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
        # feature:F7 — AD steering safety envelope limits. FINAL (fixed from a
        # real-vehicle measurement pool); see GT_esmini/config/virtual_driver.json.
        "a_lat_max_steer",
        "yaw_rate_max",
        "steer_rate_max",
        "envelope_v_floor",
        "ad_steering_envelope_steer_jerk_max",
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
        # feature:F7 (F7b) — FFB target-tracking numeric gains / thresholds.
        # Units are NORMALIZED axis-fraction (spike-calibrated); see
        # scripts/ffb_spike/README.md §1e/§2e.
        "ffb_target_track_kp",
        "ffb_target_track_kd",
        "ffb_target_track_max_force",
        "ffb_target_track_hard_stop_zone",
        "ffb_target_track_friction_ff",
        "ffb_target_track_friction_ff_eps",
        "ffb_target_track_feel_ratio",
        "ffb_target_track_override_steer_force_threshold",
        "ffb_target_track_override_steer_dev_threshold",
        "ffb_target_track_override_sustain_time",
        "ffb_target_track_override_target_rate_gate",
        "ffb_target_track_override_position_error_rate_gate",
        "ffb_target_track_override_residual_threshold",
        "ffb_target_track_override_residual_reanchor_tau",
        "ffb_target_track_override_shadow_breakaway",
        "ffb_target_track_override_shadow_breakaway_left",
        "ffb_target_track_override_shadow_breakaway_right",
        "ffb_target_track_override_shadow_motion_epsilon",
        "ffb_target_track_override_shadow_kinetic",
        "ffb_target_track_override_shadow_force_to_velocity",
        "ffb_target_track_override_shadow_v_max",
        # feature:F7 — wheel dead-time / first-order-lag + motion-onset tuning
        # (G29-measured; present in virtual_driver.json but were missing here,
        # so a PUT touching them 422'd and the GUI could not reach them).
        "ffb_target_track_override_shadow_onset_grace",
        "ffb_target_track_override_shadow_dead_time",
        "ffb_target_track_override_shadow_velocity_tau",
        "ffb_target_track_override_shadow_motion_rate_eps",
    }
)
# ffb_safety_max_saturation_seconds / ffb_safety_max_runtime_seconds /
# ffb_safety_saturation_ratio also exist in virtual_driver.json (unattended-run
# watchdog, default 0=disabled — see its "_comment_ffb_safety") but are
# DELIBERATELY left out of _NUMBER_KEYS/KNOWN_KEYS: enabling them for an
# interactive session would force-terminate the run mid-drive. GET still
# returns them verbatim (on-disk truth); PUT rejects them as unknown (422).
# Not exposed in the GUI either — see VirtualDriverPanel.tsx.
# String enum keys: 'manual' (overridable) or 'scenario' (locked-auto).
# ``input_type`` is the run's input source (see ControllerVirtualDriver.cpp's
# input-source selection): "sdl2_wheel" only when GT_ENABLE_SDL2, "network"
# drives NetworkInputBridge (what the web /ws/input override panel targets),
# anything else (including "stub") falls back to StubInputSource. Note
# "headless_ffb" is ALSO accepted by the C++ side but is deliberately NOT
# offered here — VirtualDriverConfig.hpp/ControllerVirtualDriver.cpp document
# it as existing only for the headless FFB closed-loop regression smoke
# (vd_ffb_headless_smoke.py), not for scenario/GUI runs.
_STRING_ENUM_KEYS: dict[str, frozenset[str]] = {
    "override_lateral": frozenset({"manual", "scenario"}),
    "override_longitudinal": frozenset({"manual", "scenario"}),
    "input_type": frozenset({"stub", "network", "sdl2_wheel"}),
}
# Integer-typed keys — SDL2 wheel button IDs (feature:F7 exposed the whole set
# so bindings can be remapped without a rebuild). Only used when the run's
# input_type is "sdl2_wheel". -1 = unassigned; any non-negative integer is a
# raw SDL joystick button ID (see SDL2WheelInput.Poll).
_INT_KEYS = frozenset(
    {
        "sdl2_override_button",
        "sdl2_indicator_left_button",
        "sdl2_indicator_right_button",
        "sdl2_upshift_button",
        "sdl2_downshift_button",
        "sdl2_headlight_button",
        "sdl2_high_beam_button",
        "sdl2_fog_light_button",
        "sdl2_hazard_button",
        "sdl2_auto_resume_button",
    }
)
KNOWN_KEYS = _BOOL_KEYS | _NUMBER_KEYS | frozenset(_STRING_ENUM_KEYS) | _INT_KEYS

# Owned by the per-run writer (_write_virtual_driver_config); a GUI edit must
# never override these, so PUT rejects them as unknown. input_type is NOT
# here — it moved to _STRING_ENUM_KEYS (GUI-editable) — the runner only
# defaults it (stub -> network) rather than owning it outright.
_EXCLUDED_KEYS = frozenset(
    {
        "input_port",
        "input_transport",
        "vehicle_params_file",
    }
)

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
    "_ad_steering_envelope": (
        "feature:F7 AD steering safety envelope. Clamps the AD-COMMANDED "
        "steering (never the manual input) to four physical limits: lateral "
        "accel, yaw rate, steering rate, steering jerk. Independent from "
        "max_lateral_accel above (that value is already consumed picking "
        "curve speed, so reusing it here would clamp during ordinary curve "
        "driving). Default ON (safety feature). a_lat_max_steer/yaw_rate_max/"
        "steer_rate_max are FINAL (fixed from a 15-scenario real-vehicle "
        "measurement pool; a_lat/yaw = pool max x1.3, steer_rate_max=1.5 on "
        "separate grounds — see AdSteeringEnvelope.hpp). "
        "ad_steering_envelope_steer_jerk_max=0.0 (disabled) is the shipping "
        "default: a direct closed-loop measurement of what each candidate cap "
        "does at dt=0.01 (not a distribution percentile — an earlier "
        "valley-of-the-histogram derivation was withdrawn, its instrument "
        "only resolved jerk to 1.0 /s^2) shows the effective caps are either "
        "near-invisible (25: engages 1.1-4.5% of frames, shifts the path "
        "0.001-0.004 m) or intrusive (10: engages ~50% of frames in turning "
        "runs, shifts the path >1 m); this is an optional comfort feature, "
        "not a validated safety limit, so it ships OFF pending a product "
        "decision. 0 or negative disables it, restoring the rate-only "
        "limiter bit-identically. See AdSteeringEnvelope.hpp "
        "kAdEnvelopeDefaultSteerJerkMax for the full rationale."
    ),
    "ad_steering_envelope_enabled": True,
    "a_lat_max_steer": 4.3,
    "yaw_rate_max": 1.0,
    "steer_rate_max": 1.5,
    "envelope_v_floor": 1.0,
    "ad_steering_envelope_steer_jerk_max": 0.0,
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
    "_sdl2_bindings": (
        "SDL2 wheel button IDs (only consumed when input_type=sdl2_wheel). "
        "Edit at runtime — no rebuild needed. -1 = unassigned. "
        "sdl2_auto_resume_button is feature:F7's manual->auto RESUME; RESUME "
        "is also reachable from the Web panel or the AUTO_RESUME PSTC bit."
    ),
    "sdl2_override_button": 0,
    "sdl2_indicator_left_button": 7,
    "sdl2_indicator_right_button": 6,
    "sdl2_upshift_button": 4,
    "sdl2_downshift_button": 5,
    "sdl2_headlight_button": -1,
    "sdl2_high_beam_button": -1,
    "sdl2_fog_light_button": -1,
    "sdl2_hazard_button": -1,
    "sdl2_auto_resume_button": 3,
    "_ffb_target_track": (
        "feature:F7 (F7b) FFB target-tracking — SDLFFBSink drives the wheel "
        "toward AD's commanded angle, and OverrideManager treats a sustained "
        "driver push-back as a MANUAL latch (torque-proxy). Units NORMALIZED "
        "axis-fraction (spike scripts/ffb_spike/README.md §1e/§2e). SDL2 wheel "
        "only. Default OFF so existing VD behavior is bit-identical."
    ),
    "ffb_target_track_enabled": False,
    "ffb_target_track_kp": 4.0,
    "ffb_target_track_kd": 0.35,
    "ffb_target_track_max_force": 0.6,
    "ffb_target_track_hard_stop_zone": 0.85,
    "ffb_target_track_friction_ff": 0.15,
    "ffb_target_track_friction_ff_eps": 0.01,
    "ffb_target_track_feel_ratio": 0.0,
    "ffb_target_track_override_steer_force_threshold": 0.20,
    "ffb_target_track_override_steer_dev_threshold": 0.04,
    "ffb_target_track_override_sustain_time": 0.10,
    "ffb_target_track_override_target_rate_gate": 0.30,
    "ffb_target_track_override_position_error_rate_gate": 0.10,
    "ffb_target_track_override_residual_threshold": 0.08,
    "ffb_target_track_override_residual_reanchor_tau": 1.50,
    "ffb_target_track_override_shadow_breakaway": 0.21,
    "ffb_target_track_override_shadow_breakaway_left": 0.170,
    "ffb_target_track_override_shadow_breakaway_right": 0.190,
    "ffb_target_track_override_shadow_motion_epsilon": 0.01,
    "ffb_target_track_override_shadow_kinetic": 0.16,
    "ffb_target_track_override_shadow_force_to_velocity": 3.35,
    "ffb_target_track_override_shadow_v_max": 1.00,
    "ffb_target_track_override_shadow_onset_grace": 0.05,
    "ffb_target_track_override_shadow_dead_time": 0.041,
    "ffb_target_track_override_shadow_velocity_tau": 0.018,
    "ffb_target_track_override_shadow_motion_rate_eps": 0.02,
    # ffb_safety_max_saturation_seconds / ffb_safety_max_runtime_seconds /
    # ffb_safety_saturation_ratio intentionally NOT listed here — see the
    # _NUMBER_KEYS comment above. GET still round-trips them from disk.
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
    if key in _INT_KEYS:
        # SDL joystick button IDs are integers. Accept int; reject bool/float.
        # bool must be rejected first because it's a subclass of int in Python.
        if isinstance(value, bool) or not isinstance(value, int):
            raise HTTPException(status_code=422, detail=f"'{key}' must be an integer")
        return int(value)
    # number key
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise HTTPException(status_code=422, detail=f"'{key}' must be a number")
    return float(value)


@router.get("/config")
async def get_config() -> dict[str, Any]:
    """Read current virtual_driver.json (VD planner/policy/driver settings).

    Includes the "_..." comment keys and the runner-owned ``input_port`` /
    ``input_transport`` / ``vehicle_params_file`` keys (spec documentation /
    on-disk truth). Falls back to the shipped defaults when the file is absent.
    """
    return _read_config()


@router.get("/defaults")
async def get_defaults() -> dict[str, Any]:
    """Return the factory-default virtual_driver.json values (for the Reset button)."""
    return dict(DEFAULT_VIRTUAL_DRIVER_CONFIG)


@router.put("/config")
async def update_config(patch: dict[str, Any]) -> dict[str, Any]:
    """Write virtual_driver.json.

    - Only known ``policy_*`` / planner / driver / policy-tuning keys plus
      ``input_type`` are accepted; any other non-comment key — including the
      runner-owned ``input_port`` / ``input_transport`` /
      ``vehicle_params_file`` — is rejected (422).
    - Each value is type-checked (bool / number / enum string). ``input_type``
      must be one of "stub" / "network" / "sdl2_wheel" — see
      ``_STRING_ENUM_KEYS``.
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
