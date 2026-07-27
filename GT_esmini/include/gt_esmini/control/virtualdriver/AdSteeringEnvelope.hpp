#pragma once

// feature:F7 — AD steering safety envelope (pure logic).
//
// The Pure Pursuit driver model (PIDPurePursuitDriver.cpp:30-58) is completely
// STATELESS and has no limit on lateral deviation, rate, or amplitude beyond
// the final `clamp(+-1)` on its own output. TrajectoryShortPlanner.cpp's
// anchor step anchors the preview to the CURRENT (physically occupied) lane
// center instantaneously every frame (no ramp) -- NOT "the routed lane" an
// earlier version of this comment claimed (resume_merge_trajectory_design.md
// section 2-2: pos.GetLaneId() tracks physical occupancy, it never consults
// the route). So a large raw cross-track error at the moment a manual->
// AUTO_RESUME transition fires turns directly into a maximal steering command.
// Measured with FFB fully OFF: steer_peak 0.957 / yaw rate 1.95 rad/s /
// estimated lateral accel ~29 m/s^2 — the defect is in the AD command itself,
// not the haptic loop. (feature:F7 resume-merge, shipped default OFF, now
// smooths specifically this transition by ramping a ROUTE-lane-anchored
// reference instead of snapping to it -- see ResumeMergeProfile.hpp and
// TrajectoryShortPlanner.cpp's ctx.merge_active branch. This envelope stays
// the final physical clamp regardless of whether that feature is enabled.)
//
// This module clamps an AD-commanded normalized steering angle to four
// physical limits:
//   1. lateral acceleration : |kappa| <= a_lat_max_steer / max(v, v_floor)^2
//   2. yaw rate             : |kappa| <= yaw_rate_max    / max(v, v_floor)
//   3. steering rate        : |d(delta)/dt|   <= steer_rate_max
//   4. steering jerk        : |d(rate_norm)/dt| <= steer_jerk_max, where
//      rate_norm = d(steer_norm)/dt is the normalized steering RATE (not the
//      rad-domain delta rate of (3) above) — see kAdEnvelopeDefaultSteerJerkMax
//      below for the unit rationale.
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

// feature:F7 — steering-JERK limit (a further narrowing of the steering-rate
// limiter above, not a separate feature: see AdSteeringEnvelope.cpp). Units:
// normalized-steering jerk, |d^2(steer_norm)/dt^2| [1/s^2] — NOT the same
// domain as steer_rate_max above (that one is rad/s of front-wheel angle).
// delta = steer_norm * max_steer_angle, so a normalized rate cap of
// steer_rate_max/max_steer_angle ~= 1.5/0.61 ~= 2.46 [1/s] is the
// corresponding normalized-rate ceiling this jerk limit ramps up to.
//
// The 15-scenario real-vehicle measurement pool used to fix
// a_lat_max_steer/yaw_rate_max/steer_rate_max above has no jerk numbers in it
// (only a_lat=3.289, yaw_rate=0.780, steer_rate=0.769), so the same
// "normal-driving pool max x1.3" rule cannot be applied here — the same
// situation that already forced steer_rate_max to be set on separate grounds
// (1.5, not the x1.3 value ~1.0).
//
// 25.0 was NOT derived from a distribution percentile. An earlier attempt to
// place it in the "valley" of a bimodal jerk histogram was withdrawn: the
// telemetry it was read from held normalized steering to 4 fixed-point
// decimals, which at dt=0.01 quantizes derived jerk to a 1.0 /s^2 quantum, so
// the reported "normal-driving p99 = 2.0" was two quanta of instrument floor
// rather than signal. Re-measured at 9 decimals (VirtualDriverTelemetryJson),
// the real per-run p99 of |jerk(steer_in)| is 0.36 / 1.58 / 1.23 /s^2
// (straight / right-turn / junction) against per-run maxima of 421-698 — and
// the valley itself turns out to be scenario-dependent: it exists in the two
// turning runs (pooled gap 17.9-44.8 /s^2) but NOT in the straight-line run,
// whose tail is smooth all the way up. A percentile/valley rule is therefore
// not a sound basis for this number.
//
// What 25.0 IS based on: a direct closed-loop measurement of what each
// candidate cap actually does, at the shipping dt=0.01, over the same three
// runs (instrument self-checked first: cap=0 must fire 0.00% of frames, and
// |jerk(steer_out)| must pin to the cap when set).
//
//   cap    frames where the cap engaged     trajectory shift vs cap=0
//   10     3.95% / 49.54% / 51.01%          1.19 m / 1.37 m  <- excluded
//   25     1.10% /  4.48% /  2.88%          0.001-0.004 m
//   50     0.65% /  1.80% /  1.29%          same order as 25
//
// At 25 every engagement clusters within +-0.3s of the known discrete command
// transitions (below); none occur during steady cruise, and the closed-loop
// path moves by millimetres (speed by <0.03 m/s). At 10 the cap engages on
// half of all frames in the turning runs and moves the path by more than a
// metre, which would rewrite the behavioral regression baseline. 25 is the
// point where the cap reaches the pathological transitions without reaching
// ordinary driving.
//
// Note the cap RAISES upper percentiles of |jerk(steer_out)| while lowering
// the maximum (right-turn p95 0.27 -> 15.34 at cap=25). That is the intended
// smoothing, not a regression: one 693 /s^2 spike is spread across several
// ~25 /s^2 frames.
//
// Sanity check on the same number from the time domain: at 25 /s^2 the
// normalized-rate rise from 0 to the 2.46 /s ceiling above takes 0.098s
// instead of a single frame. The "pool max x1.3" analogue (2.6) would need
// 0.95s to reach that ceiling.
//
// WHERE THE JERK COMES FROM (measured on the three hands-off real-machine
// runs, comparing envelope.steer_in against envelope.steer_out frame by
// frame). Of the frames with |jerk(steer_out)| > 50:
//   ~86% the RAW AD command was already stepwise (|jerk(steer_in)| > 25); in
//         11 of them the envelope was not acting at all and jerk_in equalled
//         jerk_out exactly.
//   ~14% the RATE LIMITER produced them, and specifically at the RELEASE of a
//         brief (1-2 frame) rate-saturation interval — a catch-up step, where
//         jerk_in was small (e.g. -3) but jerk_out reached -133.
// Saturation ENTRY cannot be a source: a rate limiter only clips |rate| down,
// so it can never produce a rate step the raw command did not demand, and the
// corner it makes on entry is bounded by the input's own jerk. Exit is the
// asymmetric case, and this cap removes it — with the cap set, the
// rate-saturation frames in the measured windows go to zero, so the catch-up
// has no condition left to occur in.
// The other ~86% is an upstream discontinuity in the AD command itself
// (short-horizon replan / Pure Pursuit target hand-over). This cap is a
// downstream mitigation of that, NOT a root fix; the upstream discontinuity
// is tracked separately.
//
// This flat /s^2 cap is the shipped form. A speed-dependent lateral-snap term
// was implemented and then removed: BOTH the analysis that motivated it and
// the counter-measurement that argued against it were taken on instruments
// later found defective, so neither supports a decision. Re-introducing a
// speed-scaled term requires fresh evidence, not the old numbers. What does
// survive is that speed did not separate the pass/fail pair in the residual
// data: their peaks occurred at 4.29 and 4.24 m/s.
// SHIPPED DEFAULT IS 0 (disabled). 25.0 is the measured candidate above, not
// the shipped value, and it must stay that way until the defect below is
// fixed.
//
// DEFECT (measured, not hypothetical): the jerk stage narrows the window AFTER
// the curvature clamp, so when the lateral-accel cap demands a fast unwind the
// jerk window can refuse to let the command get there — leaving the applied
// steering OUTSIDE the envelope's own a_lat ceiling. Measured worst case over
// a 112-cell resume sweep (dt=0.01, internally consistent rows: measured jerk
// exactly at the cap, measured rate under the rate cap): a_lat reached 4.571
// against the 4.3 ceiling at cap=25, and 5.545 at cap=10, versus 2.600 with
// the cap disabled. A safety envelope that a shaping term can push you out of
// is not a safety envelope. Enabling this by default is refused until the
// jerk window is made asymmetric — movement TOWARD the curvature-limited value
// must always be permitted, and only movement away from it may be jerk-shaped.
//
// The design reasoning that missed this: "the curvature cap varies smoothly
// with speed, so a slow approach to it is harmless". That holds in cruise and
// fails exactly where this feature is aimed — an AUTO_RESUME transient, where
// the required curvature changes fast.
constexpr double kAdEnvelopeCandidateSteerJerkMax = 25.0;  // [1/s^2] measured candidate, NOT shipped
constexpr double kAdEnvelopeDefaultSteerJerkMax   = 0.0;   // [1/s^2] shipped: disabled (see defect above)

struct AdSteeringEnvelopeConfig
{
    bool   enabled         = true;  // safety feature: default ON
    double a_lat_max_steer = kAdEnvelopeDefaultALatMaxSteer;
    double yaw_rate_max    = kAdEnvelopeDefaultYawRateMax;
    double steer_rate_max  = kAdEnvelopeDefaultSteerRateMax;
    double v_floor         = kAdEnvelopeDefaultVFloor;
    double steer_jerk_max  = kAdEnvelopeDefaultSteerJerkMax;  // <= 0 disables (bit-identical no-op)
};

// Persistent state: the caller's record of the last steering command actually
// REALIZED on the vehicle. ComputeAdSteeringEnvelope() only READS
// prev_steer_norm (as the steering-rate anchor) and prev_steer_rate_norm (as
// the steering-JERK anchor), and never writes either — see the function doc
// below for why that responsibility stays with the caller
// (UpdateAdSteeringEnvelopeState is the intended way to update both together).
struct AdSteeringEnvelopeState
{
    double prev_steer_norm      = 0.0;  // normalized [-1,1], last APPLIED command
    double prev_steer_rate_norm = 0.0;  // normalized [1/s], last REALIZED steering rate
};

// Which constraint(s) fired this frame (or none) — for telemetry/verification
// ("normal driving never trips the envelope") and debugging.
struct AdSteeringEnvelopeSnapshot
{
    bool   valid                = false;  // true whenever the envelope actually ran (enabled)
    bool   lateral_accel_active = false;  // lateral-accel cap was the binding (tightest) limit AND it clipped
    bool   yaw_rate_active      = false;  // yaw-rate cap was the binding (tightest) limit AND it clipped
    bool   steer_rate_active    = false;  // the steering-rate limiter clipped this frame
    bool   steer_jerk_active    = false;  // the steering-jerk limiter clipped this frame (further narrowed the rate window)
    bool   any_active           = false;  // OR of the four, for quick telemetry gating
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
//   state          : state.prev_steer_norm (rate-limit anchor) and
//                    state.prev_steer_rate_norm (jerk-limit anchor) are READ
//                    and never written by this function. The CALLER must
//                    update both after this call with whichever command was
//                    ACTUALLY applied this frame — the clamped AUTO output
//                    while AD owns steering, or the raw manual input while the
//                    human does — via UpdateAdSteeringEnvelopeState() below.
//                    This is what lets a manual->AUTO_RESUME transition ramp
//                    smoothly from the physical wheel angle (and its realized
//                    rate) instead of a stale AD proposal, without a
//                    dedicated "resume ramp" state machine.
//   cfg            : limits / master enable
//   out_snapshot   : optional; which constraint(s) fired this frame
//
// When cfg.enabled is false this is a bit-identical no-op: returns
// steer_norm_cmd UNCHANGED, and *out_snapshot (if non-null) has valid=false
// and nothing active, but steer_norm_in/steer_norm_out are STILL both set to
// steer_norm_cmd (echoing the pass-through) — telemetry consumers read those
// two unconditionally, and a stale 0.0 there would misread as "the envelope
// clipped this to zero" rather than "the envelope did nothing this frame".
//
// cfg.steer_jerk_max <= 0 is ALSO a bit-identical no-op onto the jerk stage
// specifically: the steering-rate limiter (3 above) runs exactly as before
// the jerk stage existed, byte-for-byte (required for the regression
// baseline's deviation=0 check).
double ComputeAdSteeringEnvelope(double                          steer_norm_cmd,
                                 double                          v,
                                 double                          wheel_base,
                                 double                          max_steer_angle,
                                 double                          dt,
                                 const AdSteeringEnvelopeState&  state,
                                 const AdSteeringEnvelopeConfig& cfg,
                                 AdSteeringEnvelopeSnapshot*     out_snapshot = nullptr);

// Update state with whichever steering command was ACTUALLY applied this
// frame (see ComputeAdSteeringEnvelope's `state` doc above): advances BOTH
// the angle anchor (prev_steer_norm) and the rate anchor
// (prev_steer_rate_norm, the steering-jerk limiter's own anchor) together, so
// callers only need one call per frame instead of hand-differentiating the
// rate themselves. dt <= 0 yields prev_steer_rate_norm = 0.0 (no realized
// rate can be attributed to a non-positive time step).
void UpdateAdSteeringEnvelopeState(AdSteeringEnvelopeState& state, double applied_steer_norm, double dt);

}  // namespace gt_esmini
