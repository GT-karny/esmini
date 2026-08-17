// req-vd-ad:REQ-AD-030 / vd-func:FUNC-081
//
// Implementation of SpeedLimiter.hpp. Every rule of substance is in that
// header (throttle-only, shared kickdown, cap selection and its unavailable-
// limit fallback); this file only implements it.

#include "gt_esmini/control/manualdrive/SpeedLimiter.hpp"

#include <algorithm>

namespace gt_esmini
{

SpeedLimiterResult ApplySpeedLimiter(const SpeedLimiterConfig& cfg,
                                     double                    throttle_in,
                                     double                    ego_speed_mps,
                                     double                    speed_limit_mps,
                                     bool                      kickdown_active)
{
    SpeedLimiterResult out;
    out.throttle_out = throttle_in;

    if (!cfg.enabled) return out;

    // Cap selection (header's "WHERE THE CAP COMES FROM"): linked mode uses the
    // road limit, but an unavailable one falls back to the setting instead of
    // becoming a 0 cap.
    double cap = cfg.set_speed_mps;
    if (cfg.speed_limit_linked && speed_limit_mps > 0.0)
    {
        cap = speed_limit_mps;
    }
    out.cap_mps = cap;

    if (cap <= 0.0) return out;  // no meaningful cap configured -> nothing to limit

    if (kickdown_active)
    {
        // Temporary release (REQ-AD-030 step b). Reported, not silent: see
        // SpeedLimiterResult::kickdown_released for why this is a separate
        // flag from `limiting`.
        out.kickdown_released = true;
        return out;
    }

    // Linear taper up to the cap, hard zero above it. One-sided by
    // construction -- `allowed` can only ever lower the pedal.
    const double band    = std::max(0.01, cfg.taper_band_mps);
    const double head    = cap - ego_speed_mps;
    const double allowed = std::min(1.0, std::max(0.0, head / band));

    if (allowed < throttle_in)
    {
        out.throttle_out = allowed;
        out.limiting     = true;
    }
    return out;
}

}  // namespace gt_esmini
