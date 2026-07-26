#pragma once

#include "osi_hostvehicledata.pb.h"

namespace gt_esmini
{

// feature:F7 (F7b) — sample used by OverrideManager to feed the manual-latch
// on a driver push against the target-tracking servo. Units are NORMALIZED
// axis-fraction (matches SDLFFBSink's actual physical wheel read and the
// spike calibration; see FfbTargetServo.hpp).
//
// target_norm is exposed so OverrideManager can compute |d(target)/dt| for the
// observational rate diagnostics (the PID servo's normal tracking lag creates
// non-zero position_error even without any driver push, so "is the target
// moving?" is the first thing a human wants to know when reading a latch
// trace). See OverrideManager::Update.
struct FfbInterventionSample
{
    double commanded_force = 0.0;   // last |u_feedback| the servo commanded, [0..max_force]
    double position_error  = 0.0;   // target - actual, [-1..1] axis-fraction
    double target_norm     = 0.0;   // AD-commanded wheel target this frame; rate-gate source
    bool   active          = false; // true only while the servo is running

    // feature:F7 — SIGNED **EFFECTIVE** force: the value actually handed to
    // the haptic device this frame, AFTER the friction feed-forward has been
    // added, AFTER the hard-stop taper, and AFTER the final clamp to
    // max_force. Nothing further is applied to it downstream — SDLFFBSink
    // passes exactly this number to UpdateConstantEffect(), and
    // HeadlessFfbInput integrates exactly this number into its plant.
    // (SDLFFBSink: sat + friction + damping + soft_stop + target_track,
    // clamped; HeadlessFfbInput: the full ComputeSteerServoForce return value
    // u = u_fb + u_ff, clamped — headless has no feel terms.) Same sign
    // convention as FfbTargetServo.hpp: POSITIVE force pushes the wheel
    // LEFT = axis NEGATIVE.
    //
    // Under the shipped config (feel_ratio = 0) the sat/friction/damping terms
    // are zero, so this reduces to the servo force alone — EXCEPT near the
    // steering lock, where soft_stop is deliberately NOT scaled by feel_ratio
    // (it is end-stop protection, not road feel). That is correct for this
    // field: the wheel really does receive that force, so the prediction must
    // account for it. "effective force ≈ servo force" is a mid-travel
    // approximation only.
    //
    // WHY THIS EXISTS, and why commanded_force must NOT be substituted for
    // it: commanded_force is the PID FEEDBACK TERM ONLY. The Coulomb friction
    // feed-forward (friction_ff, default 0.15), the hard-stop taper and the
    // final clamp are all deliberately excluded from it so the friction
    // compensation does not eat the detector's threshold margin (see
    // FfbTargetServo.hpp "Why the split").
    //
    // OverrideManager's shadow model predicts where an UNHELD wheel would be
    // by integrating a measured force→velocity plant. That plant must be fed
    // the force the wheel actually feels. Feeding it the feedback-only value
    // under-drives it by up to friction_ff = 0.15 — which is NOT negligible
    // against the measured G29 breakaway of 0.17–0.21: the prediction would
    // systematically say "the wheel cannot move" while the real wheel does
    // move, and every hands-off frame would look like a driver intervention.
    // This is a units/semantics trap, not a control-flow one: it cannot be
    // caught by reading the call graph, only by comparing prediction against
    // measurement with hands off (see scripts/vd_ffb_notouch_parity.py).
    double effective_force_signed = 0.0;
};

class IFFBSink
{
public:
    virtual ~IFFBSink() = default;
    virtual void Update(const osi3::HostVehicleData& state, double dt) = 0;
    virtual void SetEnabled(bool enabled) = 0;

    // feature:F7 (F7b) — target-tracking control.
    // Called by the AD stack (ControllerVirtualDriver) each frame to hand the
    // servo the commanded wheel angle. target_norm is [-1, +1] axis-fraction;
    // active=false disables the servo term (existing SAT/friction/damping/
    // soft-stop combined-force path is unaffected).
    // Default no-op so sinks that don't implement it (NullFFBSink initially)
    // just ignore the call.
    virtual void SetSteerTarget(double /*target_norm*/, bool /*active*/) {}

    // feature:F7 (F7b) — most recent servo state, sampled once per frame.
    // OverrideManager reads this to detect "driver pushed against the servo"
    // (see FfbInterventionSample above). Default returns an inert sample.
    virtual FfbInterventionSample GetInterventionSample() const { return {}; }
};

} // namespace gt_esmini
