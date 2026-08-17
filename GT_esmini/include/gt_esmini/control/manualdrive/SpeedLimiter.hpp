#pragma once

// req-vd-ad:REQ-AD-030 / vd-func:FUNC-081
//
// SpeedLimiter (MSL, "manually settable speed limiter") -- the LIMIT stage of
// design §3-1's three-stage longitudinal arbitration, sitting between ACC
// (generate) and AEB (safety).
//
// ============================================================================
// A LIMITER IS NOT A CONTROLLER -- THE ONE THING THIS FILE MUST NOT DO
// ============================================================================
// The limiter clamps THROTTLE and never commands brake. That is not an
// omission to be fixed later, it IS the definition of the function
// (REQ-AD-030 step a) and it has an observable consequence the requirement
// deliberately pins as a NEGATIVE: on a descent the vehicle may exceed the set
// speed and the limiter must still not brake (slug md-msl-no-brake-downhill).
// A future edit that "improves" the limiter by adding a brake term would turn
// it into a speed CONTROLLER, silently absorbing ACC's job, and would be
// invisible on flat ground -- which is why the downhill negative exists at all.
//
// The throttle clamp is one-sided: `throttle_out = min(throttle_in, allowed)`.
// The human can always ask for LESS than the limiter allows. There is no path
// in this file that raises a pedal.
//
// ============================================================================
// KICKDOWN -- THE SHARED DETECTOR, NOT A SECOND THRESHOLD
// ============================================================================
// REQ-AD-030 step b (temporary release on a floored accelerator) and
// REQ-AD-025 step d (AEB suppression) are the SAME EDGE by design (§3-3).
// This struct therefore takes `kickdown_active` as an INPUT -- the verdict of
// the one shared KickdownDetector the stack owns -- rather than comparing the
// pedal against a threshold of its own. Two thresholds would eventually
// disagree and produce the state nobody can explain from outside ("AEB was
// suppressed but the limiter never released"), which is exactly what the
// md-kickdown-shared-consistency observation is written to catch.
//
// ============================================================================
// WHERE THE CAP COMES FROM (REQ-AD-030 step c)
// ============================================================================
// `speed_limit_linked` selects between the driver's own setting and the road's
// posted limit (the same GetSpeedLimit() route REQ-AD-026 step g uses). When
// linked but the road limit is unavailable (<= 0), the limiter FALLS BACK to
// the driver's setting rather than treating 0 as a cap: an unknown limit must
// never become the strictest number in the chain, or a road without speed data
// would pin the throttle shut.

#include <algorithm>

namespace gt_esmini
{

struct SpeedLimiterConfig
{
    bool enabled = false;  // design §9: every ADAS function ships default OFF

    // Driver-set cap [m/s]. Adjusted by the SAME operating controls as ACC's
    // set speed (design §9's note that MSL shares ACC's operating vocabulary),
    // so a scenario that exercises the stalk exercises both functions' setting
    // path.
    double set_speed_mps = 0.0;

    // REQ-AD-030 step c: cap on the road's posted limit instead of the setting.
    bool speed_limit_linked = false;

    // Width of the taper below the cap, in m/s, over which the allowed throttle
    // falls linearly from full to zero. A hard cliff at the cap would make the
    // vehicle oscillate around it (full throttle at cap-epsilon, zero at
    // cap+epsilon) at frame rate. REQUIRES CALIBRATION (verification plan §5):
    // 2.0 m/s is picked to be visibly wider than one frame's speed change at
    // dt=0.05 and narrower than the setting step (1.39 m/s ~ 5 km/h) is large,
    // not measured against a powertrain.
    double taper_band_mps = 2.0;
};

struct SpeedLimiterResult
{
    double throttle_out = 0.0;

    // The cap that was actually in force this frame [m/s] -- the "effective"
    // half of REQ-AD-030 step c's two-configuration comparison (gt.msl.cap_mps).
    // 0.0 when the limiter is not enabled.
    double cap_mps = 0.0;

    // The clamp actually bit this frame (throttle_out < throttle_in). This is
    // what the gt.msl row reports as ACTIVE; being merely enabled is STANDBY.
    bool limiting = false;

    // The cap was lifted by the shared kickdown latch (REQ-AD-030 step b).
    // Reported separately from `limiting` because they are different facts:
    // during a kickdown the limiter is NOT limiting, and the reason it is not
    // is precisely this -- without the flag, a released limiter and an
    // unreached cap look identical from outside.
    bool kickdown_released = false;
};

// One frame of the limit stage. Pure: no state, no engine, no OSI.
//
//   throttle_in      : the generate stage's proposal (ACC's output, or the
//                      human's own pedal when ACC is not generating).
//   ego_speed_mps    : current speed.
//   speed_limit_mps  : road speed limit, <= 0 when unavailable.
//   kickdown_active  : the SHARED KickdownDetector's latched verdict (see the
//                      header block above -- do not re-derive it here).
SpeedLimiterResult ApplySpeedLimiter(const SpeedLimiterConfig& cfg,
                                     double                    throttle_in,
                                     double                    ego_speed_mps,
                                     double                    speed_limit_mps,
                                     bool                      kickdown_active);

}  // namespace gt_esmini
