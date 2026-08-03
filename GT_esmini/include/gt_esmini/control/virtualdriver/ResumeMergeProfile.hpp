#pragma once

// feature:F7 (AUTO_RESUME merge trajectory) -- pure-logic lateral offset
// profile generator.
//
// Background (docs/virtualdriver/design/resume_merge_trajectory_design.md section 1):
// on a manual->AUTO_RESUME transition, TrajectoryShortPlanner anchors the
// preview to the CURRENT (physically occupied) lane's CENTER instantaneously
// every frame (no ramp -- see AdSteeringEnvelope.hpp's own quote of the same
// defect). NOTE: earlier copies of this comment said "the routed lane" in all
// three places it appears; that was false and all three were corrected
// together (design doc section 2-2). pos.GetLaneId() tracks PHYSICAL
// occupancy (HVDStateApplier -> SetInertiaPos -> XYZ2TrackPos -> Track2Lane)
// and never consults the route -- measured directly: the lane id flips -3->-4
// mid-maneuver and the reported offset re-references with it
// (test_results/f7_lane_offset_semantics_probe.txt). That is why a merge must
// target the CONTROLLER-RESOLVED ROUTE lane rather than the occupied one:
// anchoring to the occupied lane makes the AD settle in the wrong lane
// instead of returning (measured: dev converges to -3.70m and stays there).
// A driver who has drifted a lane width off-route therefore hands the safety envelope a
// raw STEP reference, which the envelope can only clamp, not smooth -- the
// reference itself stays a step. This module generates a SMOOTH reference
// instead: given the captured hand-over state (initial lateral offset d0,
// lateral velocity v0_lat, lateral acceleration a0_lat) it produces a
// lateral offset target d(t) that decays from d0 to 0 over a duration T,
// matching the driver's ACTUAL initial motion (not assuming it was zero) so
// the merge starts with no discontinuity in commanded steering angle
// (design doc section 8-1-1: steering angle ~= atan(wheel_base * curvature),
// and curvature is proportional to d'').
//
// Responsibility, and ONLY this responsibility (design doc section 8-1):
// "(d0, v0_lat, a0_lat, T) から任意時刻の横オフセット目標 d(t) を返す". No esmini
// dependency: no Position, no Route, no lane/road knowledge. Resolving the
// target lane, capturing d0/v0_lat/a0_lat from the vehicle, and writing
// d(t) into preview points is the CALLER's (TrajectoryShortPlanner's)
// responsibility -- kept out of this module so the trajectory math is fully
// testable without the engine, same convention as AdSteeringEnvelope
// (control/virtualdriver/AdSteeringEnvelope.hpp) and FfbTargetServo
// (control/manualdrive/FfbTargetServo.hpp).
//
// THE CLOSED FORM (design doc section 8-2, numerically verified there to
// 1e-14 residuals, and re-derived symbolically for this header): a quintic
// in normalized time u = t/T satisfying ALL SIX boundary conditions
//     d(0)=d0, d'(0)=v0_lat, d''(0)=a0_lat, d(T)=d'(T)=d''(T)=0
// -- as opposed to an earlier design that pinned d''(0)=0 unconditionally.
// See "THE BUG A FUTURE READER WILL WANT TO REINTRODUCE" below for why that
// was wrong. With A=d0, B=v0_lat*T, C=a0_lat*T*T:
//
//   d(u) = A + B*u + (C/2)*u^2
//        + (-10*A - 6*B - 1.5*C)*u^3
//        + ( 15*A + 8*B + 1.5*C)*u^4
//        + ( -6*A - 3*B - 0.5*C)*u^5
//
// d'(t) = d'(u)/T and d''(t) = d''(u)/T^2 (chain rule on u=t/T, du/dt=1/T).
//
// THE BUG A FUTURE READER WILL WANT TO REINTRODUCE: pinning d''(0)=0 (i.e.
// assuming the driver handed over with zero curvature) looks simpler and
// "more settled" as a boundary condition, but it throws away the driver's
// ACTUAL steering angle at the hand-over instant. Measured consequence
// (design doc section 8-2, v=8 m/s, wheel_base=2.7): a driver handing over
// at a_lat=2.58 m/s^2 saw a -6.212 DEGREE steering-angle step at t=0 under
// the d''(0)=0 design -- the exact "unnatural steering" defect this feature
// exists to remove, self-inflicted. a0_lat MUST be threaded through from the
// caller's REALIZED yaw_rate*speed (design doc section 8-3(a)), not assumed
// zero.
//
// STRUCTURAL FACT (encode this, it will look like a bug to a future reader):
// because d''(0) is PINNED to a0_lat by construction, max|d''(t)| over the
// whole trajectory is ALWAYS >= |a0_lat| -- there is no T, no matter how
// long, that can make a trajectory starting at curvature a0_lat stay under a
// comfort ceiling BELOW a0_lat. Requiring max|d''| <= a_lat_comfort is
// therefore INFEASIBLE whenever |a0_lat| > a_lat_comfort, for every T (see
// ComfortBoundIsInfeasibleBelowHandoverAccel in the unit tests, which pins
// this as a fact about the math, not an implementation choice). This is why
// duration selection below bounds against
//     a_bound = max(a_lat_comfort, |a0_lat|)
// and NOT a_lat_comfort alone: never make it worse than what the driver
// handed over; if they handed over inside the comfort band, stay inside it.
// The comfort ceiling is a bound on what THIS MODULE'S OWN maneuver may ADD
// on top of the hand-over state, not a bound retroactively imposed on the
// initial condition it was handed.
//
// DURATION SELECTION is a DETERMINISTIC GRID SEARCH (design doc section
// 8-2), not iteration to convergence: T is the smallest value in
// {T_min, T_min+dT, ..., T_max} (dT = kResumeMergeDurationStepS) for which
//     max_u |d''(u; d0, v0_lat*T, a0_lat*T^2)| / T^2 <= a_bound
// evaluated over a fixed kResumeMergeCurvatureSampleCount-point grid of u in
// [0,1]. If no candidate in the grid qualifies, T = T_max and
// comfort_unmet is reported true (never silently accept an unmet bound --
// design doc section 5-4's "だまって諦めない" discipline). This is deterministic
// and grid-exact, so callers (and tests) can pin the exact T chosen rather
// than relying on iterative convergence.
//
// SIGN CONVENTION: d0, v0_lat, a0_lat are all in the SAME signed space as
// Position::GetOffset() / SetLanePos's raw +t-axis offset (design doc
// section 2-4) -- NOT lane-sign-relative. The merge trajectory decays TOWARD
// ZERO regardless of d0's sign (a positive d0 decays down through positive
// values, a negative d0 decays up through negative values), so the CALLER
// must place d(t) directly (d * (tx,ty), no lane_sign factor) -- see design
// doc section 2-4's documented historical trap ("素朴に流用すると...逆向きになる")
// for what happens when a caller re-applies a sign correction on top of an
// already-unsigned quantity. This module has no opinion on lanes at all; it
// only guarantees the OUTPUT keeps d0's sign until it reaches zero (see
// MergeDirectionOpposesOffsetSign in the unit tests).

namespace gt_esmini
{

// Shipped defaults (design doc section 8-5). Single source of truth ON THE
// C++ SIDE, same convention as AdSteeringEnvelope.hpp: config/virtual_driver.json
// and web/backend/api/virtual_driver_api.py's DEFAULT_VIRTUAL_DRIVER_CONFIG
// are a different language each and must be kept numerically in sync by hand
// when this module is wired into VirtualDriverConfig (design doc section 6
// edit point #2/#3 -- not part of this pure-logic module).
// Shipped ON since 2026-07-28. It was the top-priority request behind
// feature:F7. Real-hardware confirmation: the user (GT-karny) test-drove the
// merge on a real wheel on 2026-07-28 and reports it feels smooth -- this is
// the user's own account communicated directly in-session, not a separately
// logged/instrumented real-machine test run (no recorded date/vehicle/speed
// beyond "today, this wheel"). Headless measurement independently confirms
// the qualitative shape (GT_esmini/docs/virtualdriver/field-test/resume_merge_user_check.md
// sec 4): with this flag off the car can simply stay parked in the adjacent
// lane after AUTO_RESUME; with it on, a repeatable headless probe returns to
// the route lane in ~3.7s with zero overshoot. See that doc for the current
// jerk/lateral-accel/steer-rate numbers and their measurement method -- do
// not restate specific figures here without re-deriving them, since an
// earlier draft of this comment stated "confirmed on the real wheel" with no
// attribution at all, while resume_merge_user_check.md said in the same
// breath that real-hardware feel was NOT yet measured -- a direct
// contradiction an independent audit flagged. Attribute or don't claim it.
// The bit-identical guarantee of design doc section 3 is unchanged and still
// tested -- it is a statement about what happens when this flag is false, not
// about which way it ships. The maneuver only ever arms on an AUTO_RESUME that
// finds the car more than min_offset_m from its route lane, so a run that
// never overrides is unaffected either way (regression baselines: 22/22
// scenarios, deviation 0, with this ON).
constexpr bool   kResumeMergeDefaultEnabled       = true;
constexpr double kResumeMergeDefaultALatComfort   = 1.5;    // [m/s^2] comfort ceiling on the ADDED maneuver (see a_bound doc above)
constexpr double kResumeMergeDefaultDurationMinS  = 1.5;    // [s]
constexpr double kResumeMergeDefaultDurationMaxS  = 6.0;    // [s]
constexpr double kResumeMergeDefaultMinOffsetM    = 0.5;    // [m] below this, do not arm (ordinary lane noise must not trigger a maneuver)

// Deterministic grid-search step for T selection (design doc section 8-2).
// NOT a tuning knob: changing it changes which T candidates exist, so it is
// part of the algorithm's contract, exposed as a named constant instead of a
// config field. Tests reference this symbol instead of duplicating the
// literal.
constexpr double kResumeMergeDurationStepS = 0.05;  // [s]

// Fixed sample count for the u in [0,1] grid used to bound max|d''(u)|
// during duration selection (design doc section 8-2: "固定格子...固定サンプル数").
constexpr int kResumeMergeCurvatureSampleCount = 201;

struct ResumeMergeConfig
{
    bool   enabled        = kResumeMergeDefaultEnabled;
    double a_lat_comfort  = kResumeMergeDefaultALatComfort;
    double duration_min_s = kResumeMergeDefaultDurationMinS;
    double duration_max_s = kResumeMergeDefaultDurationMaxS;
    double min_offset_m   = kResumeMergeDefaultMinOffsetM;
};

// Captured hand-over state and the selected trajectory, persisted by the
// caller across frames (same pattern as AdSteeringEnvelopeState): Arm
// ResumeMerge captures d0/v0_lat/a0_lat ONCE and selects T ONCE (design doc
// section 8-3: "以後 T は再計算しない"); AdvanceResumeMerge then only advances
// elapsed_s.
struct ResumeMergeState
{
    bool   active        = false;  // merge in progress; false => EvaluateResumeMergeOffset reads as 0 unconditionally
    double d0            = 0.0;    // [m] captured initial lateral offset (signed, raw +t-axis -- see header doc)
    double v0_lat        = 0.0;    // [m/s] captured initial lateral velocity
    double a0_lat        = 0.0;    // [m/s^2] captured initial lateral acceleration == initial curvature proxy (design doc section 8-3)
    double duration_s    = 0.0;    // [s] selected T (design doc section 8-2 grid search); fixed for the life of this arm
    double elapsed_s     = 0.0;    // [s] time since ArmResumeMerge, advanced by AdvanceResumeMerge
    double a_bound       = 0.0;    // [m/s^2] the bound actually enforced this arm: max(a_lat_comfort, |a0_lat|)
    bool   comfort_unmet = false;  // true if even duration_max_s could not bring max|d''| under a_bound (never silently ignored -- design doc section 5-4)
};

// --- Pure quintic evaluation (design doc section 8-2 closed form) ----------
//
// Parametrized directly (no ResumeMergeState dependency): given the captured
// boundary values and a chosen duration, evaluate position / velocity /
// acceleration at any t. t is NOT range-checked to [0, duration_s] -- probing
// exactly t=0 or t=duration_s to pin a boundary condition is the intended use
// from unit tests; extrapolating far outside is the caller's responsibility,
// same as any other polynomial evaluator (duration_s is floored at 1e-9
// ONLY to avoid a divide-by-zero, never silently substituted for a sane
// duration).
//
// EvaluateResumeMergeOffset below (the production/planner-facing entry
// point) wraps EvaluateQuinticOffset with the STATE MACHINE's own domain
// rule (past T => 0, not extrapolation). That rule lives at the wrapper
// level specifically so this pure form stays usable for exact boundary-value
// tests independent of the state machine's "past T" behavior.
double EvaluateQuinticOffset(double d0, double v0_lat, double a0_lat, double duration_s, double t);
double EvaluateQuinticVelocity(double d0, double v0_lat, double a0_lat, double duration_s, double t);
double EvaluateQuinticAccel(double d0, double v0_lat, double a0_lat, double duration_s, double t);

// Deterministic grid-search duration selection (design doc section 8-2; see
// header doc above for the full rationale). Returns the chosen T; if
// out_comfort_unmet is non-null, writes whether T_max was reached without
// satisfying a_bound = max(cfg.a_lat_comfort, |a0_lat|).
double SelectResumeMergeDuration(double                   d0,
                                 double                   v0_lat,
                                 double                   a0_lat,
                                 const ResumeMergeConfig& cfg,
                                 bool*                    out_comfort_unmet = nullptr);

// --- State machine (design doc section 8-3) ---------------------------------

// Arm a new merge: captures d0/v0_lat/a0_lat and selects T ONCE via
// SelectResumeMergeDuration. Returns false (and leaves state fully
// default/inactive) when cfg.enabled is false or |d0| < cfg.min_offset_m --
// in both cases state is reset via `state = ResumeMergeState{}` FIRST, so a
// failed arm attempt can never leave a stale PREVIOUS arm's state active.
bool ArmResumeMerge(ResumeMergeState& state, double d0, double v0_lat, double a0_lat, const ResumeMergeConfig& cfg);

// Offset target at (state.elapsed_s + t_ahead_s) -- the value a preview
// point that many seconds ahead of "now" should carry (design doc section
// 2-5: placed per-point, absolute, no lane_sign). Returns 0.0 when
// !state.active, or when the queried time is at-or-past state.duration_s
// (merge complete: quietly falls back to whatever anchors the reference
// afterward, rather than extrapolating the quintic past its valid domain --
// see EvaluateQuinticOffset's doc above for why extrapolation is available
// as a raw building block but is NOT what happens here).
double EvaluateResumeMergeOffset(const ResumeMergeState& state, double t_ahead_s);

// Advance elapsed_s by dt (dt < 0 is floored to 0, same defensive convention
// as AdSteeringEnvelope's dt_safe). Deactivates the state once elapsed_s
// reaches duration_s -- the merge is then complete and EvaluateResumeMergeOffset
// starts returning 0.0 for every query.
void AdvanceResumeMerge(ResumeMergeState& state, double dt);

// Immediately deactivate (design doc section 8-3's three disarm triggers --
// storyboard lateral action, manual re-latch, route loss -- are all the
// CALLER's decision; this function only performs the resulting state
// transition). Only `active` is cleared, not the captured d0/v0_lat/a0_lat/
// duration_s/a_bound/comfort_unmet fields: EvaluateResumeMergeOffset already
// checks active first and returns 0.0 regardless of the rest, and a
// subsequent ArmResumeMerge always resets the whole struct before writing
// new values, so nothing downstream can observe stale data through either
// path -- leaving the last-captured values in place instead of zeroing them
// is a (harmless) convenience for a caller that wants to log what an
// in-progress merge was disarmed FROM.
void DisarmResumeMerge(ResumeMergeState& state);

}  // namespace gt_esmini
