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

#pragma once

#include <string>

namespace gt_esmini
{

/// Observation-based vehicle physics (pitch/roll from acceleration, RPM from speed).
/// Lightweight extraction of RealVehicle's spring-damper model that requires no
/// throttle/brake/steering input — only longitudinal and lateral accelerations.
class ObservedVehiclePhysics
{
public:
    struct Params
    {
        double pitch_stiffness = 10.0;
        double pitch_damping   = 2.0;
        double roll_stiffness  = 12.0;
        double roll_damping    = 3.0;
        double mass_height     = 0.05;
        double max_pitch_deg   = 5.0;
        double max_roll_deg    = 5.0;
    };

    ObservedVehiclePhysics() = default;
    explicit ObservedVehiclePhysics(const Params& params) : params_(params) {}

    /// Advance the spring-damper model by dt seconds.
    /// @param dt          Time step [s]
    /// @param long_acc    Longitudinal acceleration [m/s^2] (positive = forward accel)
    /// @param lat_acc     Lateral acceleration [m/s^2] (positive = left turn centripetal)
    void Update(double dt, double long_acc, double lat_acc);

    /// Reset internal state (rates and angles) to zero.
    void Reset();

    double GetDynamicPitch() const { return dynamic_pitch_; }
    double GetDynamicRoll()  const { return dynamic_roll_; }

    const Params& GetParams() const { return params_; }
    void SetParams(const Params& p) { params_ = p; }

    /// Estimate engine RPM from absolute speed (same logic as HVDEstimator).
    static double EstimateRPM(double abs_speed);

private:
    Params params_;

    double dynamic_pitch_ = 0.0;
    double dynamic_roll_  = 0.0;
    double pitch_rate_    = 0.0;
    double roll_rate_     = 0.0;
};

} // namespace gt_esmini
