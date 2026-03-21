/*
 * GT_esmini - Extended esmini with Vehicle Physics
 * https://github.com/esmini/esmini
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2024 GT_esmini contributors
 */

#include "gt_esmini/control/ObservedVehiclePhysics.hpp"

#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace gt_esmini
{

static double Clamp(double v, double lo, double hi)
{
    return std::max(lo, std::min(hi, v));
}

void ObservedVehiclePhysics::Update(double dt, double long_acc, double lat_acc)
{
    if (dt <= 0.0)
    {
        return;
    }

    // --- Pitch (longitudinal acceleration → nose pitch) ---
    // Same spring-damper model as RealVehicle::UpdatePhysics lines 298-330
    // Positive long_acc (forward accel) → negative forcing → nose down
    double pitch_forcing = -params_.mass_height * long_acc;

    double pitch_acc = (-params_.pitch_stiffness * dynamic_pitch_)
                     - (params_.pitch_damping * pitch_rate_)
                     + pitch_forcing;
    pitch_rate_    += pitch_acc * dt;
    dynamic_pitch_ += pitch_rate_ * dt;

    // --- Roll (lateral acceleration → body roll) ---
    // Same spring-damper model as RealVehicle::UpdatePhysics lines 332-340
    // Positive lat_acc (left turn centripetal) → positive forcing → roll right
    double roll_forcing = params_.mass_height * lat_acc;

    double roll_acc = (-params_.roll_stiffness * dynamic_roll_)
                    - (params_.roll_damping * roll_rate_)
                    + roll_forcing;
    roll_rate_    += roll_acc * dt;
    dynamic_roll_ += roll_rate_ * dt;

    // --- Clamp dynamic angles ---
    double lim_p = params_.max_pitch_deg * M_PI / 180.0;
    double lim_r = params_.max_roll_deg  * M_PI / 180.0;
    dynamic_pitch_ = Clamp(dynamic_pitch_, -lim_p, lim_p);
    dynamic_roll_  = Clamp(dynamic_roll_,  -lim_r, lim_r);
}

void ObservedVehiclePhysics::Reset()
{
    dynamic_pitch_ = 0.0;
    dynamic_roll_  = 0.0;
    pitch_rate_    = 0.0;
    roll_rate_     = 0.0;
}

double ObservedVehiclePhysics::EstimateRPM(double abs_speed)
{
    // Same logic as HVDEstimator::EstimateRPM
    constexpr double kWheelRadius = 0.35;
    constexpr double kGearRatio   = 3.5;
    constexpr double kIdleRPM     = 800.0;
    constexpr double kMaxRPM      = 7000.0;

    double wheel_rps  = abs_speed / (2.0 * M_PI * kWheelRadius);
    double engine_rpm = wheel_rps * 60.0 * kGearRatio;

    return Clamp(engine_rpm, kIdleRPM, kMaxRPM);
}

} // namespace gt_esmini
