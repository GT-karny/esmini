#include "gt_esmini/control/virtualdriver/AdSteeringEnvelope.hpp"

#include <algorithm>
#include <cmath>

namespace gt_esmini
{

double ComputeAdSteeringEnvelope(double                          steer_norm_cmd,
                                 double                          v,
                                 double                          wheel_base,
                                 double                          max_steer_angle,
                                 double                          dt,
                                 const AdSteeringEnvelopeState&  state,
                                 const AdSteeringEnvelopeConfig& cfg,
                                 AdSteeringEnvelopeSnapshot*     out_snapshot)
{
    if (out_snapshot) *out_snapshot = AdSteeringEnvelopeSnapshot{};

    if (!cfg.enabled)
    {
        // Bit-identical no-op on the RETURN VALUE, but steer_norm_in/out must
        // still echo the pass-through — telemetry consumers read them
        // unconditionally, and a stale 0.0 here would misread as "the
        // envelope clipped this to zero" instead of "the envelope did
        // nothing" (see header doc: valid=false / nothing active is how
        // "disabled" is actually signaled).
        if (out_snapshot)
        {
            out_snapshot->steer_norm_in  = steer_norm_cmd;
            out_snapshot->steer_norm_out = steer_norm_cmd;
        }
        return steer_norm_cmd;
    }

    const double wb  = std::max(wheel_base, 1.0e-3);
    const double msa = std::max(max_steer_angle, 1.0e-6);
    const double dt_safe = std::max(dt, 0.0);

    // v_floor is also guarded against a misconfigured (<=0) value here — this
    // must never divide by zero regardless of what the config says.
    const double v_eff = std::max({v, cfg.v_floor, 1.0e-6});

    const double kappa_lat_max = cfg.a_lat_max_steer / (v_eff * v_eff);
    const double kappa_yaw_max = cfg.yaw_rate_max / v_eff;
    const double kappa_max     = std::min(kappa_lat_max, kappa_yaw_max);

    // Same normalized<->radian<->curvature relation PIDPurePursuitDriver uses,
    // inverted (see header doc).
    const double delta_cmd = steer_norm_cmd * msa;
    const double kappa_cmd = std::tan(delta_cmd) / wb;

    // Whichever cap is the tighter (smaller) one is the constraint actually
    // responsible for any clipping below; a tie attributes to both.
    const bool lat_binding    = kappa_lat_max <= kappa_yaw_max;
    const bool yaw_binding    = kappa_yaw_max <= kappa_lat_max;
    const bool kappa_clipped  = std::fabs(kappa_cmd) > kappa_max;

    const double kappa_lim           = std::clamp(kappa_cmd, -kappa_max, kappa_max);
    const double delta_after_lat_yaw = std::atan(kappa_lim * wb);

    // Steering-rate limit, anchored on the last ACTUALLY applied command (see
    // header — the caller owns updating state.prev_steer_norm).
    const double delta_prev = state.prev_steer_norm * msa;
    const double max_step   = cfg.steer_rate_max * dt_safe;
    const double delta_lo   = delta_prev - max_step;
    const double delta_hi   = delta_prev + max_step;
    const bool   rate_clipped = (delta_after_lat_yaw < delta_lo) || (delta_after_lat_yaw > delta_hi);
    const double delta_final  = std::clamp(delta_after_lat_yaw, delta_lo, delta_hi);

    const double steer_norm_out = std::clamp(delta_final / msa, -1.0, 1.0);

    if (out_snapshot)
    {
        out_snapshot->valid                = true;
        out_snapshot->lateral_accel_active = kappa_clipped && lat_binding;
        out_snapshot->yaw_rate_active      = kappa_clipped && yaw_binding;
        out_snapshot->steer_rate_active    = rate_clipped;
        out_snapshot->any_active           = out_snapshot->lateral_accel_active ||
                                             out_snapshot->yaw_rate_active ||
                                             out_snapshot->steer_rate_active;
        out_snapshot->kappa_cmd            = kappa_cmd;
        out_snapshot->kappa_limit          = kappa_max;
        out_snapshot->steer_norm_in        = steer_norm_cmd;
        out_snapshot->steer_norm_out       = steer_norm_out;
    }

    return steer_norm_out;
}

}  // namespace gt_esmini
