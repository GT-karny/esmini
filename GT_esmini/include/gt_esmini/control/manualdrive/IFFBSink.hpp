#pragma once

#include "osi_hostvehicledata.pb.h"

namespace gt_esmini
{

// feature:F7 (F7b) — sample used by OverrideManager to feed the manual-latch
// on a driver push against the target-tracking servo. Units are NORMALIZED
// axis-fraction (matches SDLFFBSink's actual physical wheel read and the
// spike calibration; see FfbTargetServo.hpp).
struct FfbInterventionSample
{
    double commanded_force = 0.0;   // last |u| the servo commanded, [0..max_force]
    double position_error  = 0.0;   // target - actual, [-1..1] axis-fraction
    bool   active          = false; // true only while the servo is running
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
