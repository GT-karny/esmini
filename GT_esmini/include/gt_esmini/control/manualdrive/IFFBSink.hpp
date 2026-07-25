#pragma once

#include "osi_hostvehicledata.pb.h"

namespace gt_esmini
{

// feature:F7 (F7b) — sample used by OverrideManager to feed the manual-latch
// on a driver push against the target-tracking servo. Units are NORMALIZED
// axis-fraction (matches SDLFFBSink's actual physical wheel read and the
// spike calibration; see FfbTargetServo.hpp).
//
// target_norm is exposed so OverrideManager can compute |d(target)/dt| and
// SUPPRESS torque-proxy detection while the target is moving fast — the PID
// servo's normal tracking lag creates non-zero position_error even without
// any driver push, and the Day-1 spike (scripts/ffb_spike/05_torque_proxy.py)
// only calibrated the |u|/|dev| thresholds against a STATIC target
// (target=0), so a naive threshold check spuriously latches on every curve
// / lane change / any AD-driven steering transient (real-machine bug found
// after commit a43e4c67). The rate gate keeps the threshold-based detector
// honest — see OverrideManager::Update.
struct FfbInterventionSample
{
    double commanded_force = 0.0;   // last |u| the servo commanded, [0..max_force]
    double position_error  = 0.0;   // target - actual, [-1..1] axis-fraction
    double target_norm     = 0.0;   // AD-commanded wheel target this frame; rate-gate source
    bool   active          = false; // true only while the servo is running

    // feature:F7 (F7b, follow-up post-93b2c6c4) — SIGNED feedback-only servo
    // force (same computation as commanded_force, before the abs()). Needed by
    // OverrideManager's velocity-opposition detector: a servo alone can only
    // push the physical wheel in the direction that reduces tracking error, so
    // sign(commanded_force_signed) tracks sign(d(actual)/dt) whenever the
    // driver isn't touching the wheel — regardless of how fast the AD target
    // itself is moving. Comparing this against the wheel's own rate of motion
    // (derived by OverrideManager from target_norm/position_error history) is
    // therefore a target-motion-INVARIANT way to detect "driver pushing back",
    // unlike the two rate gates above which assume a roughly-static target.
    // See OverrideManager.cpp Update() for the full detector.
    double commanded_force_signed = 0.0;
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
