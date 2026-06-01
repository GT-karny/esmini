#include "gt_esmini/control/virtualdriver/PIDPurePursuitDriver.hpp"

#include <algorithm>
#include <cmath>

namespace gt_esmini
{

void PIDPurePursuitDriver::Reset()
{
    integral_       = 0.0;
    prev_speed_err_ = 0.0;
    has_prev_       = false;
}

PedalSteerCommand PIDPurePursuitDriver::Compute(const ShortPlannerSnapshot& plan,
                                                const DriverState&          state,
                                                double                      dt,
                                                DriverModelSnapshot*        out_snapshot)
{
    PedalSteerCommand   cmd;
    DriverModelSnapshot snap;

    if (!plan.valid || plan.preview.empty())
    {
        if (out_snapshot) *out_snapshot = snap;  // invalid
        return cmd;                              // coast: zero command
    }

    // ---- Lateral: Pure Pursuit ----
    const double v  = std::max(0.0, state.speed);
    const double Ld = std::clamp(cfg_.lookahead_gain * v, cfg_.min_lookahead, cfg_.max_lookahead);

    // Pick the first preview point at distance >= Ld (else the last point).
    const TrajectoryPoint* lp = &plan.preview.back();
    for (const auto& p : plan.preview)
    {
        const double dx = p.x - state.x;
        const double dy = p.y - state.y;
        if (std::hypot(dx, dy) >= Ld)
        {
            lp = &p;
            break;
        }
    }

    const double dx = lp->x - state.x;
    const double dy = lp->y - state.y;
    const double ch = std::cos(state.h);
    const double sh = std::sin(state.h);
    const double local_x =  dx * ch + dy * sh;
    const double local_y = -dx * sh + dy * ch;
    const double ld_actual = std::max(1e-3, std::hypot(dx, dy));

    const double alpha     = std::atan2(local_y, local_x);          // +left
    const double curvature = 2.0 * std::sin(alpha) / ld_actual;     // +left
    const double delta     = std::atan(state.wheel_base * curvature);  // wheel angle, +left
    cmd.steering = std::clamp(cfg_.steering_sign * delta / cfg_.max_steer_angle, -1.0, 1.0);

    // Cross-track error: lateral offset of the nearest preview point.
    const TrajectoryPoint& fp = plan.preview.front();
    const double front_local_y = -(fp.x - state.x) * sh + (fp.y - state.y) * ch;

    // ---- Longitudinal: Speed PID with anti-windup ----
    const double v_target = plan.preview.front().v;
    const double err = v_target - state.speed;
    const double d_term = (has_prev_ && dt > 1e-6) ? cfg_.kd * (err - prev_speed_err_) / dt : 0.0;

    double u = cfg_.kp * err + cfg_.ki * integral_ + d_term;
    // Conditional integration: stop accumulating when the output is already
    // saturated and the error would push it further into saturation. Prevents
    // the windup that otherwise causes large speed overshoot after a long accel.
    const bool sat_hi = (u >= cfg_.max_throttle) && (err > 0.0);
    const bool sat_lo = (u <= -cfg_.max_brake) && (err < 0.0);
    if (!sat_hi && !sat_lo)
    {
        integral_ += err * dt;
        const double i_max = cfg_.integral_limit / std::max(1e-6, cfg_.ki);
        integral_ = std::clamp(integral_, -i_max, i_max);
        u = cfg_.kp * err + cfg_.ki * integral_ + d_term;
    }
    prev_speed_err_ = err;
    has_prev_ = true;

    if (u >= 0.0)
    {
        cmd.throttle = std::min(u, cfg_.max_throttle);
        cmd.brake    = 0.0;
    }
    else
    {
        cmd.brake    = std::min(-u, cfg_.max_brake);
        cmd.throttle = 0.0;
    }

    // ---- Snapshot ----
    snap.throttle       = cmd.throttle;
    snap.brake          = cmd.brake;
    snap.steer          = cmd.steering;
    snap.lateral_error  = front_local_y;
    snap.heading_error  = alpha;
    snap.speed_error    = err;
    snap.lookahead_dist = Ld;
    snap.valid          = true;
    if (out_snapshot) *out_snapshot = snap;

    return cmd;
}

}  // namespace gt_esmini
