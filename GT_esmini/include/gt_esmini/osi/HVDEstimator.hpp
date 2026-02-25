/*
 * GT_esmini - Extended esmini with HostVehicleData Functionality
 * https://github.com/esmini/esmini
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2024 GT_esmini contributors
 */

#pragma once

#include <map>

namespace scenarioengine
{
class Object;
}

namespace gt_esmini
{

/**
 * @brief Estimates HostVehicleData fields from observable vehicle state.
 *
 * Used when no GT custom controller (RealDriver/PythonDriver) is active,
 * so that HostVehicleData still contains plausible throttle, brake,
 * steering, gear, RPM, and torque values reverse-engineered from behavior.
 */
class HVDEstimator
{
public:
    struct EstimatedInputs
    {
        double throttle  = 0.0;  // [0, 1]
        double brake     = 0.0;  // [0, 1]
        double steering  = 0.0;  // tire angle [rad]
        int    gear      = 1;    // -1=R, 0=N, 1=D
        double rpm       = 0.0;  // estimated engine RPM
        double torque    = 0.0;  // estimated torque [0-1 normalized]
        int    lightMask = 0;    // from VehicleLightExtension
    };

    /**
     * Compute estimated HVD fields from observable state of the given object.
     * Call once per frame per target vehicle when no GT controller is active.
     * @param obj The scenario Object (should be VEHICLE type)
     * @param dt  Frame timestep [s]
     * @return Estimated inputs
     */
    EstimatedInputs Estimate(scenarioengine::Object* obj, double dt);

    /**
     * Reset internal state (call on scenario close / re-init)
     */
    void Reset();

private:
    struct VehicleCache
    {
        double prev_speed  = 0.0;
        bool   initialized = false;
    };

    std::map<int, VehicleCache> cache_;

    // Physics constants (matching RealVehicle defaults for plausible output)
    static constexpr double kIdleRPM       = 800.0;
    static constexpr double kMaxRPM        = 7000.0;
    static constexpr double kGearRatio     = 3.5;
    static constexpr double kWheelRadius   = 0.32;   // [m]
    static constexpr double kDefaultMaxAcc = 10.0;    // [m/s^2]
    static constexpr double kDefaultMaxDec = 10.0;    // [m/s^2]
    static constexpr double kDragCoeff     = 0.005;
    static constexpr double kSpeedThreshold = 0.01;   // [m/s] threshold for standstill

    double EstimateRPM(double abs_speed) const;
    double EstimateTorque(double rpm) const;
    static int BuildLightMaskForObject(scenarioengine::Object* obj);
};

} // namespace gt_esmini
