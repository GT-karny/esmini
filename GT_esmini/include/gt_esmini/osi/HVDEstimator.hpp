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
        double prev_speed    = 0.0;
        double prev_steering = 0.0;
        double prev_throttle = 0.0;
        double prev_brake    = 0.0;
        double prev_ratio    = 1.0;  // smoothed attenuation ratio
        bool   initialized   = false;
    };

    std::map<int, VehicleCache> cache_;

    // Physics constants (matching RealVehicle defaults for plausible output)
    static constexpr double kIdleRPM       = 700.0;
    static constexpr double kMaxRPM        = 6500.0;
    static constexpr double kGearRatio     = 3.5;
    static constexpr double kWheelRadius   = 0.32;   // [m]
    static constexpr double kDefaultMaxAcc = 4.0;     // [m/s^2] (Corolla/Civic class)
    static constexpr double kDefaultMaxDec = 10.0;    // [m/s^2]
    static constexpr double kDragCoeff     = 0.0013;
    static constexpr double kTorquePeakPos = 0.65;    // Normalized RPM for peak torque
    static constexpr double kTorqueMin     = 0.3;     // Min normalized torque at idle/redline
    static constexpr double kSpeedThreshold = 0.01;   // [m/s] threshold for standstill
    // Preview-point steering model:
    // Instead of filtering the raw heading-rate-based wheel angle (which lags
    // through intersections), compute steering from the heading error to a
    // look-ahead point on the road network. This naturally unwinds steering
    // before exiting a curve — matching real driver behavior.
    static constexpr double kPreviewTime    = 0.8;    // [s] look-ahead time (speed-proportional)
    static constexpr double kPreviewDistMin = 2.0;    // [m] minimum preview distance (low speed / standstill)
    static constexpr double kPreviewDistMax = 30.0;   // [m] maximum preview distance (highway cap)
    static constexpr double kSteerEmaAlpha  = 0.5;    // EMA smoothing factor (higher = more responsive)
    static constexpr double kRatioEmaAlpha  = 0.3;    // EMA for attenuation ratio (prevents jumps on lane/road change)
    static constexpr double kPedalSmoothAlpha   = 0.3;  // EMA factor for throttle/brake (lower = smoother)

    double EstimateRPM(double abs_speed) const;
    double EstimateTorque(double rpm) const;
    static int BuildLightMaskForObject(scenarioengine::Object* obj);
};

} // namespace gt_esmini
