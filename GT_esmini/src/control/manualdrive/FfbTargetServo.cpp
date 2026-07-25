#include "gt_esmini/control/manualdrive/FfbTargetServo.hpp"

#include <algorithm>
#include <cmath>

namespace gt_esmini
{

double ComputeSteerServoForce(double target_norm, double actual_norm,
                              double dt, SteerServoState& state,
                              const SteerServoConfig& cfg)
{
    // Clamp dt away from 0 so the D term never explodes on the caller's edge
    // cases (paused sim, first frame after resume, ...). 1 ms is safely below
    // the 250 Hz update the spike measured.
    const double dt_safe = std::max(dt, 1.0e-3);

    const double err  = target_norm - actual_norm;
    const double derr = state.primed ? (err - state.prev_err) / dt_safe : 0.0;
    state.prev_err = err;
    state.primed   = true;

    // Sign flip: on G29 via SDL2, positive CONSTANT level pushes wheel LEFT
    // (axis negative). To servo toward +target we need NEGATIVE force. See
    // scripts/ffb_spike/README.md §1f for the diagnostic that established this.
    double u = -(cfg.kp * err + cfg.kd * derr);

    // Clamp to per-tick force cap first.
    u = std::clamp(u, -cfg.max_force, cfg.max_force);

    // Hard-stop taper (spike §3d): near the physical stop, do not push the
    // wheel further into the stop. "Outward" = force sign OPPOSITE to actual
    // sign (positive-axis right; positive force pushes LEFT; so a wheel at
    // +0.9 with force -0.5 is being pushed further RIGHT toward +1.0 = outward).
    const double a = std::abs(actual_norm);
    const bool   outward = (u * actual_norm) < 0.0;
    if (outward && a > cfg.hard_stop_zone)
    {
        const double denom = std::max(1.0e-6, 1.0 - cfg.hard_stop_zone);
        const double t     = std::clamp((a - cfg.hard_stop_zone) / denom, 0.0, 1.0);
        const double taper = 1.0 - t;   // 1.0 at zone edge, 0.0 at full lock
        u *= taper;
    }

    return u;
}

} // namespace gt_esmini
