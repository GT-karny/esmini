#pragma once

// feature:F7 — AD steering safety envelope (pure logic).
//
// The Pure Pursuit driver model (PIDPurePursuitDriver.cpp:30-58) is completely
// STATELESS and has no limit on lateral deviation, rate, or amplitude beyond
// the final `clamp(+-1)` on its own output. TrajectoryShortPlanner.cpp:139-140
// anchors the preview to the routed lane CENTER instantaneously every frame
// (no ramp), so a large raw cross-track error at the moment a manual->
// AUTO_RESUME transition fires turns directly into a maximal steering command.
// Measured with FFB fully OFF: steer_peak 0.957 / yaw rate 1.95 rad/s /
// estimated lateral accel ~29 m/s^2 — the defect is in the AD command itself,
// not the haptic loop.
//
// This module clamps an AD-commanded normalized steering angle to three
// physical limits:
//   1. lateral acceleration : |kappa| <= a_lat_max_steer / max(v, v_floor)^2
//   2. yaw rate             : |kappa| <= yaw_rate_max    / max(v, v_floor)
//   3. steering rate        : |d(delta)/dt| <= steer_rate_max
// where kappa [1/m] is the path curvature implied by the commanded steering
// and delta [rad] is the front-wheel angle.
//
// Deliberately INDEPENDENT of VirtualDriverConfig::max_lateral_accel
// (consumed by ManeuverAwareSpeedPlanner.cpp:75 alone): that value already
// picks the curve speed that puts lateral accel AT max_lateral_accel, so an
// ordinary curve already runs with lateral accel pinned near that number.
// Reusing the same value here would make this envelope clamp during routine
// curve driving instead of only the pathological resume case it exists for.
//
// Pure logic, no esmini dependency, unit-testable in isolation — same
// convention as FfbTargetServo (control/manualdrive/FfbTargetServo.hpp).

namespace gt_esmini
{

// FINAL — fixed from a real-vehicle measurement pool (feature:F7). Normal-
// driving peaks (15-scenario pool) were a_lat=3.289 m/s^2, yaw_rate=0.780
// rad/s, steer_rate=0.769 rad/s; a_lat_max_steer and yaw_rate_max are that
// pool max x1.3 (a_lat rounded to 4.3), stretching time-to-full-lock from
// ~0.12s to ~0.61s while clearing every measured normal-driving frame with
// margin.
//
// steer_rate_max is 1.5, not the same x1.3 rule (which would give ~1.0): a
// wider 27-scenario / 19,557-frame junction-batch pass found scenario
// 07_oncoming_yield__p017 (v=1.47 m/s oncoming-yield creep, ordinary PID
// micro-oscillation — not a bug) reaching steer_rate=0.964 rad/s, i.e. 96.4%
// of a 1.0 cap. A flat rate cap is physically mismatched anyway: what matters
// is the lateral JERK a steering-rate change produces, which scales with
// speed, so a flat cap is needlessly tight at creep speed and needlessly
// loose at highway speed. Raising to 1.5 takes the thinnest scenario's margin
// from 3.6% to 56%. Safety cost is small: at the AUTO_RESUME speeds this
// feature targets (e.g. 8 m/s), a_lat_max_steer is the binding cap
// (a_lat<=4.3 -> kappa<=0.0672 -> delta<=0.200rad -> steer_norm<=0.33 vs the
// pre-fix 0.96), so steer_rate_max is a secondary guard there — 1.5 rad/s
// still suppresses the measured pathological rates (3.66-3.84 rad/s) by
// ~2.5x and the worst real regression anomaly (14.6 rad/s) by ~9.7x.
//
// Single source of truth ON THE C++ SIDE: AdSteeringEnvelopeConfig's own
// default member initializers below AND VirtualDriverConfig's mirrored
// ad_steering_envelope_* fields both read these constants, so a
// recalibration only touches this block. config/virtual_driver.json and
// web/backend/api/virtual_driver_api.py's DEFAULT_VIRTUAL_DRIVER_CONFIG are a
// different language each and must be kept numerically in sync by hand.
constexpr double kAdEnvelopeDefaultALatMaxSteer = 4.3;  // [m/s^2] (3.289 measured x1.3)
constexpr double kAdEnvelopeDefaultYawRateMax   = 1.0;  // [rad/s] (0.780 measured x1.3)
constexpr double kAdEnvelopeDefaultSteerRateMax = 1.5;  // [rad/s], front-wheel-angle rate (see rationale above; near-miss 0.964 measured against the old 1.0 cap)
constexpr double kAdEnvelopeDefaultVFloor       = 1.0;  // [m/s]

struct AdSteeringEnvelopeConfig
{
    bool   enabled         = true;  // safety feature: default ON
    double a_lat_max_steer = kAdEnvelopeDefaultALatMaxSteer;
    double yaw_rate_max    = kAdEnvelopeDefaultYawRateMax;
    double steer_rate_max  = kAdEnvelopeDefaultSteerRateMax;
    double v_floor         = kAdEnvelopeDefaultVFloor;
};

// Persistent state: the caller's record of the last steering command actually
// REALIZED on the vehicle. ComputeAdSteeringEnvelope() only READS
// prev_steer_norm (as the steering-rate anchor) and never writes it — see the
// function doc below for why that responsibility stays with the caller.
struct AdSteeringEnvelopeState
{
    double prev_steer_norm = 0.0;  // normalized [-1,1], last APPLIED command
};

// Which constraint(s) fired this frame (or none) — for telemetry/verification
// ("normal driving never trips the envelope") and debugging.
struct AdSteeringEnvelopeSnapshot
{
    bool   valid                = false;  // true whenever the envelope actually ran (enabled)
    bool   lateral_accel_active = false;  // lateral-accel cap was the binding (tightest) limit AND it clipped
    bool   yaw_rate_active      = false;  // yaw-rate cap was the binding (tightest) limit AND it clipped
    bool   steer_rate_active    = false;  // the steering-rate limiter clipped this frame
    bool   any_active           = false;  // OR of the three, for quick telemetry gating
    double kappa_cmd            = 0.0;    // curvature implied by the raw command [1/m]
    double kappa_limit          = 0.0;    // effective curvature cap this frame (min of lat/yaw caps) [1/m]
    // Always populated, enabled or not (the pass-through when disabled sets
    // both to steer_norm_cmd) — telemetry compares these two to see the
    // envelope's actual effect, so neither may silently stay 0.0.
    double steer_norm_in        = 0.0;
    double steer_norm_out       = 0.0;
};

// Clamp an AD-commanded normalized steering angle to the physical envelope.
//
//   steer_norm_cmd : raw AD command, normalized [-1,1]
//   v              : ego speed [m/s]
//   wheel_base     : [m]
//   max_steer_angle: physical max front-wheel angle [rad] — the SAME constant
//                    PIDPurePursuitDriver uses (PIDPurePursuitConfig::max_steer_angle
//                    / VirtualDriverConfig::max_steer_angle), so the
//                    normalized<->radian conversion matches the driver model
//                    exactly: delta = steer_norm * max_steer_angle,
//                    kappa = tan(delta) / wheel_base (PIDPurePursuitDriver.cpp:57
//                    computes delta = atan(wheel_base * curvature); this is
//                    that relation inverted).
//   dt             : seconds since the last call
//   state          : state.prev_steer_norm is READ (as the rate-limit anchor)
//                    and never written by this function. The CALLER must
//                    update it after this call with whichever command was
//                    ACTUALLY applied this frame — the clamped AUTO output
//                    while AD owns steering, or the raw manual input while the
//                    human does. This is what lets a manual->AUTO_RESUME
//                    transition ramp smoothly from the physical wheel angle
//                    instead of a stale AD proposal, without a dedicated
//                    "resume ramp" state machine.
//   cfg            : limits / master enable
//   out_snapshot   : optional; which constraint(s) fired this frame
//
// When cfg.enabled is false this is a bit-identical no-op: returns
// steer_norm_cmd UNCHANGED, and *out_snapshot (if non-null) has valid=false
// and nothing active, but steer_norm_in/steer_norm_out are STILL both set to
// steer_norm_cmd (echoing the pass-through) — telemetry consumers read those
// two unconditionally, and a stale 0.0 there would misread as "the envelope
// clipped this to zero" rather than "the envelope did nothing this frame".
double ComputeAdSteeringEnvelope(double                          steer_norm_cmd,
                                 double                          v,
                                 double                          wheel_base,
                                 double                          max_steer_angle,
                                 double                          dt,
                                 const AdSteeringEnvelopeState&  state,
                                 const AdSteeringEnvelopeConfig& cfg,
                                 AdSteeringEnvelopeSnapshot*     out_snapshot = nullptr);

}  // namespace gt_esmini
