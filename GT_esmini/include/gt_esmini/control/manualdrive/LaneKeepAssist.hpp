#pragma once

// req-vd-ad:REQ-AD-027 / req-vd-ad:REQ-AD-028 / vd-func:FUNC-080
//
// LaneKeepAssist (LKA, and its warning-only degeneration LDW) -- the LATERAL
// half of the ManualDrive ADAS coexistence stack (design
// manualdrive_adas_design.md §5). This is phase D, and it is the first phase
// whose main body is a NEW component rather than a wiring of existing policy
// bodies: nothing in the VirtualDriver stack computes "distance from the lane
// centre -> corrective steering". PIDPurePursuitDriver follows a trajectory
// preview point list and never looks at the lane centre at all (design §5-1,
// investigation 2026-08-04), so there was nothing to reuse.
//
// Pure logic: no esmini, no OSI, no controller state -- the same convention as
// AccLonController / PedalArbitrator / KickdownDetector / AdSteeringEnvelope.
// Everything carrying a decision lives here so it is unit-testable without the
// engine; the engine-facing part (reading Position, ownership, reporting) is
// AdasCoexistenceStack's.
//
// ============================================================================
// THE SIGN CHAIN -- READ THIS BEFORE TOUCHING ANY SIGN IN THIS FILE
// ============================================================================
// A sign error here does not degrade the feature, it INVERTS it: the assist
// drives the car out of the lane it is supposed to hold. Three different
// conventions meet in this component, so the chain is pinned explicitly and the
// left/right drift asset PAIR (md_lka_drift_left + md_lka_drift_right) exists
// partly to catch a regression in it from the outside.
//
//   1. INPUTS to this file are VEHICLE-LEFT POSITIVE. `lane_offset_m` > 0 means
//      the vehicle sits LEFT of the lane centre as the DRIVER sees it, and
//      `lateral_speed_mps` > 0 means it is MOVING left. The caller converts:
//      roadmanager's Position::GetOffset() is +t (road-left along increasing s),
//      which equals vehicle-RIGHT for a vehicle driving against s, so the caller
//      multiplies by GetDrivingDirectionRelativeRoad()'s sign -- exactly what
//      AutoLightController.cpp already does for the same reason.
//
//   2. OUTPUT is in the PedalSteerCommand::steering convention, where POSITIVE
//      IS RIGHT. This is not a guess: RealVehicle::StepLateralAndAttitude
//      computes `target_wheel_angle = -steering * steer_gain`, and a positive
//      front-wheel angle turns left in the kinematic bicycle model, so positive
//      steering turns right. IndicatorFSM's own doc comment states the same
//      convention ("-1.0(left) ~ +1.0(right)").
//
//   3. THEREFORE the correction law's gains are POSITIVE-signed:
//        correction = kp * lane_offset_m + kd * lateral_speed_mps
//      Drifting LEFT (offset > 0) yields a POSITIVE correction, i.e. steer
//      RIGHT, i.e. back toward the centre. If a future edit makes this
//      expression negative, the drift assets fail in a very visible way (the
//      ego leaves the lane faster than with the assist switched off) -- which
//      is the intended failure mode, not a subtle one.
//
// ============================================================================
// WHY THE DIRECT THRESHOLD IS ON THE **MARGIN**, NOT ON THE RAW OFFSET
// ============================================================================
// design §5-1 sketches the trigger as "TLC below a threshold, OR the offset
// above a direct threshold". This file implements the second half as a
// threshold on
//
//     margin = lane_half_width - |offset| - vehicle_half_width
//
// which is the SAME quantity restated: at a fixed lane and vehicle width the
// two differ by a constant. Margin is the form used here for two reasons.
//
//   * It is the dimension the requirement is actually about. REQ-AD-027 is
//     about not crossing the line; the distance from the vehicle's SIDE to the
//     line is that distance. A raw-offset threshold is the same statement only
//     as long as nobody changes the road or the vehicle -- and this project has
//     a documented history of thresholds that stopped meaning what they said
//     when the thing under them moved.
//   * TLC is already computed from the margin (it is margin / |lateral speed|),
//     so a margin threshold and the TLC threshold are two conditions on ONE
//     geometric quantity rather than on two that could drift apart.
//
// The vehicle half width is supplied by the caller from the ego's own bounding
// box, so a wide vehicle in a narrow lane gets a smaller margin without any
// special case here.
//
// ============================================================================
// JUDGEMENT AND OUTPUT ARE SEPARATE CODE PATHS (REQ-AD-027 step f)
// ============================================================================
// `warning_only` (LDW) must leave cmd.steering BIT-IDENTICAL while the
// departure judgement keeps running and keeps warning. That is a structural
// claim, so it is met structurally: ComputeLaneKeepAssist first resolves the
// JUDGEMENT (in_speed_band / departing / warning / the suppressions) with no
// reference to warning_only at all, and only then does a separate, clearly
// fenced block produce `correction` -- a block the warning_only branch does not
// enter. There is no path on which a warning_only frame can reach the envelope.
//
// The unit tests pin this the only way that is worth anything: the SAME input,
// evaluated twice with warning_only false and true, must produce an IDENTICAL
// `warning`/`departing` verdict and a `correction` of exactly 0.0 in the second.
//
// ============================================================================
// HUMAN STEERING PRIORITY (design §5-3) -- WHY NOT OverrideManager
// ============================================================================
// Two conditions hold the assist off, and they are NOT the same fact, so they
// are reported separately:
//
//   * INDICATOR ACTIVE -- the driver has declared an intentional lane change.
//     This suppresses the CORRECTION **and the WARNING**: warning a driver
//     about a departure they signalled is the false alarm every real LDW
//     suppresses, and it is what md-lka-indicator-suppression asserts.
//   * STEERING RATE ABOVE THRESHOLD -- an unmistakable steering input. This
//     suppresses the CORRECTION but NOT the warning: a driver hauling the wheel
//     across a line without signalling is precisely who the warning is for.
//     Latched with a release so a single fast input holds the assist off for
//     `steer_override_hold_s` rather than for one frame; see the field.
//
// A SUPPRESSION STOPS THE ASSIST ON THE SAME FRAME -- it is not ramped down
// through the envelope. REQ-AD-027 step b's word is 即時中断 and design §5-3
// repeats it. A rate-limited release would keep the assist pushing for several
// frames against a driver who has just declared, with the indicator or with the
// wheel, that they are steering deliberately; "human steering priority" that
// persists for 0.2 s is not human steering priority. The envelope's ramp-down
// is used only for the OTHER way a correction ends -- the departure itself
// ceasing -- where nobody has objected and a one-frame drop is just a jolt.
//
// OverrideManager's FFB residual latch is deliberately NOT reused. Its
// semantics are the mirror image of what is needed here -- it detects a human
// TAKING the wheel back from an AD that is holding it -- whereas LKA starts from
// the human already holding the wheel and asks whether they are steering
// deliberately. Reusing a detector because it is "about the driver and the
// wheel" would inherit a threshold calibrated against a completely different
// question (requirement note, design §5-3).
//
// ============================================================================
// SPEED BAND (REQ-AD-027 step e)
// ============================================================================
// Outside the band the function reports STANDBY and produces nothing -- NOT
// UNAVAILABLE. "Switched off" and "temporarily out of its operating range" are
// different facts about a function that is installed and armed, and REQ-AD-028
// step a's three-value discipline is what keeps them apart. Same key vocabulary
// as ACC (min_speed_mps / max_speed_mps, max <= 0 meaning no upper bound), by
// the shared decision recorded on REQ-AD-026/027.

#include <cstdint>

namespace gt_esmini
{

struct LaneKeepAssistConfig
{
    bool enabled      = false;  // design §9: every ADAS function ships default OFF
    bool warning_only = false;  // true = LDW: judge and warn, never steer

    // ---- availability band (REQ-AD-027 step e) -----------------------------
    // REQUIRES CALIBRATION (verification plan §5). Production LKA systems are
    // commonly quoted as arming around 60 km/h, but that figure is a
    // second-hand customary value, not a measurement, and the default here is
    // 0 (no lower bound) so that turning the function on never silently does
    // nothing on a scenario that happens to run slower. A scenario that wants
    // the band tested sets it explicitly, which is exactly what
    // md-lka-speed-range-gate's two configurations do.
    double min_speed_mps = 0.0;
    double max_speed_mps = 0.0;  // <= 0 means no upper bound (same as ACC)

    // ---- departure judgement (design §5-1) ---------------------------------
    // Time to line crossing, in seconds. Only meaningful while the vehicle is
    // moving OUTWARD; an inward-moving vehicle has no crossing to be early for.
    // REQUIRES CALIBRATION.
    double tlc_threshold_s = 1.5;

    // The direct geometric trigger, on the MARGIN (see the header block for why
    // margin and not raw offset). Engage at or below `margin_threshold_m`,
    // release only above `release_margin_m`. The gap between them is the
    // hysteresis band and it must be positive: without it the assist chatters
    // on and off at frame rate exactly at the trigger, which shows up in the
    // OSI stream as a state column nobody can read.
    //
    // SIZED AGAINST THE ROAD, NOT PICKED ROUND. The first values here (0.30 /
    // 0.50) were chosen before anyone looked at a lane: on this project's own
    // straight_500m_2lane.xodr the lane is 3.07 m wide and the verification
    // vehicle is 2.00 m, so the TOTAL margin at dead centre is 0.535 m. A
    // 0.50 m release threshold would have meant the assist could only release
    // within 3.5 cm of the centre line -- effectively never, i.e. a hysteresis
    // band that silently became a latch. 0.15 / 0.30 leave a real band (engage
    // at |offset| >= 0.385, release at |offset| <= 0.235) with room on both
    // sides. STILL REQUIRES CALIBRATION -- this is arithmetic against one road,
    // not a measurement of what a driver finds acceptable.
    double margin_threshold_m = 0.15;
    double release_margin_m   = 0.30;

    // ---- correction law ----------------------------------------------------
    // Gains on the (vehicle-left-positive) offset and lateral speed. Both
    // POSITIVE -- see the SIGN CHAIN block.
    //
    // SIZED FROM THE STEERING THE JOB ACTUALLY NEEDS, for the same reason the
    // thresholds above are. Nulling a 0.3 m/s lateral drift at 25 m/s needs a
    // heading change of ~0.012 rad; taking about a second, that is a yaw rate
    // of 0.012 rad/s, i.e. a curvature of 0.012/25 = 4.8e-4 /m, i.e. a
    // front-wheel angle of atan(4.8e-4 * 3.0 m) = 1.4e-3 rad, i.e. a NORMALIZED
    // steering of 1.4e-3 / 0.61 (RealVehicle's steer_gain) = 0.0024. Corrections
    // live around 0.01, not around 0.1 -- the original 0.06 / 0.25 gains were
    // more than an order of magnitude too large and would have produced a
    // violent snap-and-oscillate rather than an assist. These values give
    // 0.012*0.4 + 0.020*0.3 = 0.011 for a typical departure (0.4 m off centre,
    // drifting at 0.3 m/s). REQUIRES CALIBRATION.
    double kp_offset  = 0.012;  // [steer-norm per metre]
    double kd_lateral = 0.020;  // [steer-norm per (m/s)]

    // ---- the lateral envelope (design §5-2) --------------------------------
    // THE AMPLITUDE LIMIT IS THE FEATURE, NOT A SAFETY NET. LKA is a
    // correction the human must always be able to overpower, so this bound is
    // what makes "assist" true rather than "control": the human's own steering
    // spans [-1, 1] and the assist may only add `correction_max` on top of it.
    // At 0.03 the driver retains more than 30x the assist's authority, and the
    // bound still sits ~3x above the typical correction derived above -- enough
    // headroom for a hard pull-back, far short of anything that could take the
    // car somewhere the driver did not.
    //
    // A rate limit accompanies it for the same reason AdSteeringEnvelope has
    // one: a step change in commanded steering is felt as a jolt through the
    // wheel and is not something a corrective assist should ever produce. At
    // 0.10 /s the assist reaches full authority in 0.3 s.
    // REQUIRES CALIBRATION (both).
    double correction_max      = 0.03;  // [steer-norm], amplitude
    double correction_rate_max = 0.10;  // [steer-norm / s]

    // ---- human steering priority (design §5-3) -----------------------------
    // |d(driver steering)/dt| at or above this counts as a deliberate steering
    // input and holds the assist off.
    //
    // SIZED AGAINST WHAT A LANE CHANGE ACTUALLY LOOKS LIKE. The first value here
    // was 0.35 /s, "well below a real lane-change input" -- which turned out to
    // be false by more than an order of magnitude, and would have made this
    // detector unreachable in ordinary driving. Moving one lane (3 m) in 3 s at
    // 25 m/s needs a lateral acceleration of ~1.3 m/s^2, i.e. a curvature of
    // 1.3/625 = 2.1e-3 /m, i.e. a front-wheel angle of 6.3e-3 rad, i.e. a
    // NORMALIZED steering of about 0.010 -- reached over roughly a second, so an
    // input RATE around 0.01 /s. A threshold of 0.35 /s is 35x that: the driver
    // would have to move the wheel violently before the assist noticed them.
    // 0.03 /s sits about 3x above an ordinary lane-change input (a driver
    // reaching that 0.010 in a third of a second rather than in one). The
    // separation is only a factor of three, and that is the honest ceiling
    // here: this vehicle's steer_gain is 0.61 rad, so a NORMALIZED input of
    // 0.05 already commands a 0.25 rad/s yaw rate at 25 m/s -- a swerve, not a
    // manoeuvre. There is simply not much room between "a lane change" and
    // "violent" to put a threshold in.
    //
    // REQUIRES CALIBRATION, AND SPECIFICALLY ON A REAL WHEEL -- there is a known
    // hazard here that the automated gate CANNOT see. ScriptedInputSource replays
    // a piecewise-linear profile with no noise, but a real G29's column jitter is
    // ~0.005 axis (scripts/ffb_spike/CHARACTERIZATION.md), which at dt=0.05
    // presents as an apparent rate of ~0.1 /s -- ABOVE this threshold. On a real
    // wheel this detector would therefore fire on a driver sitting still, and the
    // fix (a higher threshold, or filtering the input before differencing it)
    // cannot be chosen from synthetic data. Logged as a real-machine item
    // (test_results/f7_realmachine_checklist.md).
    double steer_override_rate = 0.06;  // [steer-norm / s]

    // How long the steering-input suppression is HELD after the rate falls back
    // under the threshold. Without a hold, suppression would end on the very
    // next frame of a steady deliberate steer (whose RATE is zero once the
    // driver stops moving the wheel), and the assist would fight a hand that is
    // still deliberately holding the wheel over.
    //
    // THE HOLD IS A PROXY FOR A MEASUREMENT THIS PROJECT CANNOT MAKE HEADLESS.
    // What should hold the assist off is the driver's hand still being ON the
    // wheel, i.e. steering TORQUE -- which is exactly the quantity a synthetic
    // input source does not have. Rate-plus-hold approximates it: an input that
    // moved recently is treated as an input still being made. 2.0 s covers a
    // turn-in / hold / turn-back manoeuvre without the suppression lapsing in
    // the middle of it, which is what a 1.0 s hold did (the assist came back
    // and fought a hand that had not moved). REQUIRES CALIBRATION, and the
    // calibration that would actually settle it is a real-wheel torque
    // measurement (test_results/f7_realmachine_checklist.md).
    double steer_override_hold_s = 2.0;
};

// One frame's environment. Every geometric quantity is VEHICLE-LEFT POSITIVE
// (see the header's SIGN CHAIN block); the caller does the conversion.
struct LkaFrameInput
{
    // False when the caller could not resolve lane geometry this frame (no
    // road, no lane, zero-width lane). The judgement is then NOT run and
    // nothing is reported as measured -- a fabricated 0.0 offset would look
    // exactly like a perfectly centred vehicle, which is the single most
    // dangerous default this component could have.
    bool   lane_valid         = false;
    double lane_offset_m      = 0.0;  // + = vehicle sits LEFT of the lane centre
    double lane_half_width_m  = 0.0;
    double vehicle_half_width_m = 0.0;
    double lateral_speed_mps  = 0.0;  // + = moving LEFT
    double ego_speed_mps      = 0.0;

    double driver_steering    = 0.0;  // raw human command, [-1, 1], + = right
    bool   indicator_active   = false;

    // Set by the caller when this controller does NOT own the lateral domain
    // (design §2-3). Handled here rather than by an early return in the caller
    // so that the not-owned frame still advances nothing and reports nothing --
    // the same "leave the latches frozen, do not evolve them on data we are
    // refusing to act on" rule AdasCoexistenceStack applies longitudinally.
    bool   owns_lateral       = false;
};

struct LkaFrameOutput
{
    // ---- judgement (runs in every mode, including warning_only) -------------
    bool   evaluated      = false;  // the judgement actually ran this frame
    bool   in_speed_band  = false;
    bool   departing      = false;  // the departure verdict itself
    bool   warning        = false;  // departing, minus the indicator suppression -> gt.lka.warning / the LDW row
    double margin_m       = 0.0;    // lane_half_width - |offset| - vehicle_half_width
    double tlc_s          = -1.0;   // < 0 = not applicable (not moving outward / stopped)

    // ---- suppressions -------------------------------------------------------
    bool   suppressed_indicator = false;
    bool   suppressed_steer     = false;

    // req-vd-ad:REQ-AD-028 段b producer -- the STEERING-origin driver override,
    // the third and last of the three paths that requirement's step b names
    // (accelerator: phase B, brake: phase C).
    //
    // Equal to `suppressed_steer`: true for as long as the driver's steering
    // input is holding the assist off, and NOT additionally gated on a departure
    // standing this frame. That is the same latched, input-tracking condition
    // phase B chose for the accelerator and phase C for the brake, and it was
    // re-learned here the hard way: the narrow version (`&& departing`) blinked
    // off for a single frame in md_lka_human_steer, at the instant the vehicle
    // crossed the centre of a lane it was steering through, while the driver's
    // hand did exactly the same thing before and after. An override channel that
    // tracks the geometry instead of the driver is not reporting what it claims
    // to report. The narrow fact stays observable as gt.lka.departure.
    //
    // The INDICATOR suppression deliberately does NOT raise this. OSI's Reason
    // enum has exactly two values and the indicator is not a steering input;
    // reporting it as REASON_STEERING_INPUT would misreport which control the
    // human used. It stays observable as gt.lka.suppressed_indicator.
    bool   driver_override_steering = false;

    // ---- output (warning_only never reaches this) ---------------------------
    bool   correcting     = false;  // a non-zero correction was applied this frame
    double correction     = 0.0;    // added to cmd.steering, [-correction_max, +correction_max]
    double steer_out      = 0.0;    // driver_steering + correction, clamped to [-1, 1]
};

// Cross-frame state. Passed by reference like KickdownDetector/PedalArbitrator
// so ComputeLaneKeepAssist stays a free function whose every decision is
// directly testable.
struct LaneKeepAssistState
{
    bool   departing_latched     = false;  // the engage/release hysteresis
    double prev_driver_steering  = 0.0;
    bool   prev_steering_valid   = false;  // no rate can be derived from the first frame
    double steer_override_timer  = 0.0;    // seconds of suppression still to run
    double prev_correction       = 0.0;    // the rate limiter's anchor
};

// Amplitude + rate clamp on the correction (design §5-2's "AdSteeringEnvelope と
// 同じ様式の横版"). Split out as its own free function, and NOT folded into the
// correction law, because the two answer different questions: the law decides
// which way and how hard to pull, the envelope decides what the assist is
// ALLOWED to do regardless. Keeping them apart is what lets a unit test assert
// the envelope's bound over inputs the law would never produce.
//
// STAGE ORDER IS amplitude -> rate -> AMPLITUDE AGAIN, and the repeat is
// load-bearing. The rate stage contracts toward `prev`, which is only inside the
// bound if the bound has not moved since `prev` was produced -- so a tightened
// `max_amplitude` (a reloaded config, or 0 to disable the output while leaving
// the judgement running) would otherwise leave the "authority limit" equal to
// whatever last frame happened to be. This is the exact asymmetry that keeps
// AdSteeringEnvelope's jerk stage shipped-disabled; here it is fixed instead,
// because the amplitude is a hard authority limit and the rate limit is comfort,
// so the amplitude wins when they conflict. On any ordinary frame the second
// clamp is a no-op and the rate limit is fully intact.
//
// dt <= 0 applies the amplitude clamp but NOT the rate clamp: no rate can be
// attributed to a non-positive time step (the same discipline
// AdSteeringEnvelope and ComputeMeasuredDecel already follow).
double ApplyLkaCorrectionEnvelope(double raw, double prev, double max_amplitude, double max_rate, double dt);

// One frame of the assist. See this header's blocks for the semantics; the
// implementation only implements them.
LkaFrameOutput ComputeLaneKeepAssist(const LaneKeepAssistConfig& cfg,
                                     const LkaFrameInput&        in,
                                     double                      dt,
                                     LaneKeepAssistState&        state);

// design §2-3 applied to the LATERAL domain: does the LKA CORRECTION path act
// on this frame's cmd.steering at all?
//
// THIS EXISTS TO BE SHARED WITH THE COORDINATOR, NOT AS A CONVENIENCE. The FFB
// target servo hop in ManualDriveCoordinator::RunFrame (step 6a) routes a
// steering target from the LATERAL OWNER into the wheel, and it runs exactly
// when this controller does NOT own the lateral domain. LKA writes cmd.steering
// exactly when it DOES. The two are therefore mutually exclusive by
// construction rather than by review -- and the unit test
// (LkaCorrectionAndFfbPeerRoutingAreMutuallyExclusive) pins that by evaluating
// this predicate against the coordinator's own `!owns_lateral` branch condition
// over every combination. A refactor that made the servo hop unconditional
// would put the correction on the wheel twice: once through cmd.steering into
// the physics, and again as a servo target pushing the physical wheel.
bool ManualLkaArbitrates(bool owns_lateral, bool lka_enabled, bool warning_only);

}  // namespace gt_esmini
