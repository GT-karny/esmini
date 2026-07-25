#pragma once

// feature:F7 (F7b) — FFB target-angle tracking servo (pure logic).
//
// Pure PID + hard-stop taper for driving a haptic wheel to a commanded
// target angle. Extracted so it is testable without SDL2 (SDLFFBSink calls
// the same function). All quantities are in NORMALIZED axis-fraction units
// (matching the spike calibration in scripts/ffb_spike/04_constant_pid_servo.py
// and 05_torque_proxy.py — Kp/Kd tuned in axis-fraction space, not radians).
//
// Sign convention (spike §1f): on G29 via SDL2, positive CONSTANT force pushes
// the wheel toward the LEFT hard-stop (axis NEGATIVE direction). To servo
// toward a positive target (wheel to the RIGHT), the commanded force must be
// NEGATIVE. That's why the formula ends up with `u = -(Kp*err + Kd*derr)`.

namespace gt_esmini
{

struct SteerServoConfig
{
    // PID gains. Defaults from spike §1e (Kp=4.0, Kd=0.35) measured with
    // Kp=4.0 tracking a sine 0.5 Hz target within ~17% mean error on G29.
    double kp = 4.0;
    double kd = 0.35;
    // Per-tick force cap in [0, 1]. Spike used 0.6.
    double max_force = 0.6;
    // Hard-stop taper: full force below hard_stop_zone, linear ramp to 0 at
    // full lock (|actual|=1). Only applied when force pushes further outward
    // (i.e., sign(force) opposite to sign(actual) — see comment above).
    double hard_stop_zone = 0.85;

    // Coulomb friction feed-forward magnitude. A pure-P servo can only produce
    // Kp*err of force, so any error below F_break/Kp cannot break the wheel's
    // static friction — with the shipped Kp=4.0 and the G29's measured
    // F_break≈0.19 that deadband is 0.047, which swallows an entire AD lane
    // change (whose steering command peaks at 0.065). Compensating the known,
    // roughly constant friction torque directly removes the deadband without
    // touching Kp — see scripts/ffb_spike/CHARACTERIZATION.md §4/§7.
    //
    // SAFETY INVARIANT: friction_ff MUST stay below the wheel's minimum
    // breakaway force (G29 measured: 0.170). At or above it, the feed-forward
    // alone could start the wheel moving with no position error to correct.
    // Default 0.15 keeps a 0.02 margin; measured self-creep = 0.
    double friction_ff     = 0.15;
    // Error scale the feed-forward ramps in over (tanh knee). Small enough that
    // it is ~saturated by |err|=0.02, large enough not to chatter at err≈0.
    double friction_ff_eps = 0.01;
};

struct SteerServoState
{
    double prev_err = 0.0;
    bool   primed   = false;  // false on first call so derivative starts at 0
};

// Compute one servo tick.
//   target_norm : commanded wheel position, [-1, +1] axis-fraction
//   actual_norm : measured physical wheel position, [-1, +1] axis-fraction
//   dt          : seconds since last call (used for D term; caller clamps <1ms → 1ms)
//   state       : per-servo state (call reset() to re-prime)
//   cfg         : gains / clamps
//   out_feedback: if non-null, receives the FEEDBACK-ONLY force, i.e. the same
//                 result with the Coulomb feed-forward term excluded.
// Returns commanded force in the same convention as SDL_HapticEffect CONSTANT
// level after normalization, i.e., [-max_force, +max_force] where negative
// pushes the wheel RIGHT (positive-axis motion) on G29.
//
// Why the split: OverrideManager's torque proxy asks "how hard is the driver
// resisting?", and the answer must be the servo's FEEDBACK effort. The friction
// feed-forward is a fixed compensation for a known plant property, present
// whenever err != 0 and carrying no information about the driver. Feeding the
// combined force to the detector would put a standing ~0.15 bias under a 0.20
// threshold, leaving almost no false-positive margin. Measured hands-off:
// combined force exceeds 0.20 for 1.5% of a curve, feedback-only for 0.8%
// (CHARACTERIZATION.md §6/§8c).
double ComputeSteerServoForce(double target_norm, double actual_norm,
                              double dt, SteerServoState& state,
                              const SteerServoConfig& cfg,
                              double* out_feedback = nullptr);

// Re-prime the derivative on the next call (call when target_active transitions
// false→true so we don't inject a bogus D spike from stale prev_err).
inline void ResetSteerServo(SteerServoState& s)
{
    s.prev_err = 0.0;
    s.primed   = false;
}

} // namespace gt_esmini
