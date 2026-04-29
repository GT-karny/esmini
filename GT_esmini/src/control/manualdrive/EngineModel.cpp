/*
 * GT_esmini - Extended esmini with HostVehicleData Functionality
 * https://github.com/esmini/esmini
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "gt_esmini/control/manualdrive/EngineModel.hpp"

#include <algorithm>

namespace gt_esmini
{

namespace
{
double SmoothStep01(double x)
{
    x = std::clamp(x, 0.0, 1.0);
    return x * x * (3.0 - 2.0 * x);
}
}

double EngineModel::MaxTorqueAt(double rpm) const
{
    const auto& p = params_;
    if (rpm <= p.idle_rpm)
    {
        // Below idle: torque is largely irrelevant (governor takes over).
        // Return a small fraction so the engine can pull off idle.
        return p.torque_peak_nm * 0.4;
    }
    if (rpm < p.torque_flat_low_rpm)
    {
        // Rising region: smoothstep from ~40% peak to 100% peak.
        double x = (rpm - p.idle_rpm) / std::max(1.0, p.torque_flat_low_rpm - p.idle_rpm);
        double s = SmoothStep01(x);
        return p.torque_peak_nm * (0.4 + 0.6 * s);
    }
    if (rpm <= p.torque_flat_high_rpm)
    {
        return p.torque_peak_nm;
    }
    if (rpm <= p.max_rpm)
    {
        // Falloff: linear from peak at flat_high to redline_factor*peak at max.
        double span = std::max(1.0, p.max_rpm - p.torque_flat_high_rpm);
        double t    = std::clamp((rpm - p.torque_flat_high_rpm) / span, 0.0, 1.0);
        double end_factor = std::clamp(p.torque_redline_factor, 0.1, 1.0);
        return p.torque_peak_nm * (1.0 - t * (1.0 - end_factor));
    }
    // Above redline: rev limiter handled in Step()
    return p.torque_peak_nm * std::clamp(p.torque_redline_factor, 0.1, 1.0);
}

void EngineModel::Step(double throttle, double target_rpm, bool clutch_locked, double dt)
{
    const auto& p = params_;

    if (!state_.initialized)
    {
        state_.rpm = std::max(p.idle_rpm, target_rpm);
        state_.initialized = true;
    }

    // Idle governor: if engine is below idle and driver is off-throttle,
    // inject a minimum throttle floor to keep it from stalling.
    double t = std::clamp(throttle, 0.0, 1.0);
    if (state_.rpm < p.idle_rpm * 1.05)
    {
        double idle_error = (p.idle_rpm - state_.rpm) / std::max(1.0, p.idle_rpm);
        double floor = std::clamp(p.idle_governor_gain * idle_error, 0.0, 0.25);
        t = std::max(t, floor);
    }

    // Rev limiter (soft): cut fuel above rev_limit_rpm
    state_.rev_limited = (state_.rpm > p.rev_limit_rpm);
    double t_eff = state_.rev_limited ? 0.0 : t;

    // Torque output = throttle * max torque at current RPM
    double t_max = MaxTorqueAt(state_.rpm);
    state_.torque_nm = t_eff * t_max;

    // RPM dynamics
    // - Locked clutch: RPM is yanked toward target_rpm (wheel-driven), with
    //   1st-order lag representing combined inertia of engine + driveline.
    // - Unlocked: engine free-revs toward an open-loop target proportional to
    //   throttle (so neutral/idle blip works).
    double goal;
    if (clutch_locked)
    {
        goal = std::max(p.idle_rpm, target_rpm);
    }
    else
    {
        // Free rev target: blend idle..max_rpm by throttle.
        goal = p.idle_rpm + (p.max_rpm - p.idle_rpm) * t;
    }
    if (state_.rev_limited)
    {
        goal = std::min(goal, p.rev_limit_rpm);
    }

    double tau   = std::max(1e-3, p.engine_inertia_tau_s);
    double alpha = (dt > 0.0) ? (dt / (tau + dt)) : 1.0;
    state_.rpm += alpha * (goal - state_.rpm);

    // Clamp to physical bounds
    state_.rpm = std::clamp(state_.rpm, p.idle_rpm * 0.7, p.max_rpm * 1.05);
}

void EngineModel::Reset()
{
    state_ = State{};
}

} // namespace gt_esmini
