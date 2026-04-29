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

void EngineModel::Step(double throttle, double target_rpm, bool clutch_locked,
                       const VehicleContext& vctx, double dt)
{
    const auto& p = params_;

    if (!state_.initialized)
    {
        state_.base_rpm    = std::max(p.idle_rpm, target_rpm);
        state_.rpm         = state_.base_rpm;
        state_.initialized = true;
    }

    // Idle governor: if engine is below idle and driver is off-throttle,
    // inject a minimum throttle floor to keep it from stalling. Uses base_rpm
    // (jitter-free) so the governor doesn't react to noise.
    double t = std::clamp(throttle, 0.0, 1.0);
    if (state_.base_rpm < p.idle_rpm * 1.05)
    {
        double idle_error = (p.idle_rpm - state_.base_rpm) / std::max(1.0, p.idle_rpm);
        double floor = std::clamp(p.idle_governor_gain * idle_error, 0.0, 0.25);
        t = std::max(t, floor);
    }

    // Blip injection: synthetic throttle floor while transient is active.
    bool blipping = state_.blip_timer_s > 0.0;
    if (blipping)
    {
        t = std::max(t, std::clamp(p.blip_throttle_floor, 0.0, 1.0));
    }

    // Rev limiter / torque output / creep all use base_rpm so jitter does not
    // perturb downstream physics (only the displayed RPM should fluctuate).
    state_.rev_limited = (state_.base_rpm > p.rev_limit_rpm);
    double t_eff = state_.rev_limited ? 0.0 : t;

    double t_max = MaxTorqueAt(state_.base_rpm);
    state_.torque_nm = t_eff * t_max;

    if (clutch_locked && t < 0.05 && state_.base_rpm < p.idle_rpm * 1.5)
    {
        state_.torque_nm = std::max(state_.torque_nm, p.idle_creep_torque_nm);
    }

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
    if (blipping)
    {
        goal = std::max(goal, target_rpm + state_.blip_lift_rpm);
    }
    if (state_.rev_limited)
    {
        goal = std::min(goal, p.rev_limit_rpm);
    }

    // Use a faster inertia constant during blip so RPM rises crisply.
    double tau_base = std::max(1e-3, p.engine_inertia_tau_s);
    double tau      = blipping ? std::max(1e-3, p.blip_inertia_tau_s) : tau_base;
    double alpha = (dt > 0.0) ? (dt / (tau + dt)) : 1.0;

    // Smooth base RPM evolves through the lag filter only. Jitter is added on
    // top of the displayed value but NOT folded back into base_rpm — otherwise
    // the OU offset would feed back through the lag filter and accumulate well
    // beyond its nominal std-dev.
    state_.base_rpm += alpha * (goal - state_.base_rpm);
    state_.base_rpm = std::clamp(state_.base_rpm, p.idle_rpm * 0.7, p.max_rpm * 1.05);

    double jitter_offset = 0.0;
    if (!blipping)
    {
        double slip = std::clamp(vctx.slip_factor, 0.0, 1.0);
        double idle_weight = 1.0 - slip;
        jitter_offset = idle_weight * jitter_.Step(dt);
    }
    state_.rpm = state_.base_rpm + jitter_offset;

    if (blipping)
    {
        state_.blip_timer_s = std::max(0.0, state_.blip_timer_s - dt);
        if (state_.blip_timer_s <= 0.0)
        {
            state_.blip_lift_rpm = 0.0;
        }
    }
}

void EngineModel::TriggerBlip(double duration_s, double lift_rpm)
{
    state_.blip_timer_s  = std::max(state_.blip_timer_s, duration_s);
    state_.blip_lift_rpm = std::max(state_.blip_lift_rpm, lift_rpm);
}

void EngineModel::Reset()
{
    state_ = State{};
}

} // namespace gt_esmini
