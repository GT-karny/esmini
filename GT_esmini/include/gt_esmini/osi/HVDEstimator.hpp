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
#include <string>
#include <vector>

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
 *
 * Pedal estimation is force-based (inverse longitudinal dynamics with
 * grade compensation). Gear selection uses a 2D shift schedule
 * (speed + throttle) with hysteresis and minimum hold time. RPM has a
 * 1st-order lag and low-speed clutch lockup interpolation.
 */
class HVDEstimator
{
public:
    struct EstimatedInputs
    {
        double throttle  = 0.0;  // [0, 1]
        double brake     = 0.0;  // [0, 1]
        double steering  = 0.0;  // tire angle [rad]
        int    gear      = 1;    // -1=R, 0=N, 1..N=forward gears
        double rpm       = 0.0;  // estimated engine RPM
        double torque    = 0.0;  // estimated torque [0-1 normalized]
        int    lightMask = 0;    // from VehicleLightExtension
    };

    EstimatedInputs Estimate(scenarioengine::Object* obj, double dt);

    void Reset();

    /// Load shift schedule and pedal estimation parameters from
    /// real_vehicle_params.json. Safe to call multiple times.
    void LoadParams(const std::string& configPath);

    /// Switch active drive mode (e.g. "comfort", "sport"). Unknown modes are
    /// ignored and a warning is printed. Returns true if the mode was applied.
    bool SetActiveMode(const std::string& mode);

    /// Currently active mode name.
    const std::string& GetActiveMode() const { return active_mode_; }

private:
    struct VehicleCache
    {
        double prev_speed     = 0.0;
        double prev_steering  = 0.0;
        double prev_throttle  = 0.0;
        double prev_brake     = 0.0;
        double prev_rpm       = 0.0;
        int    current_gear   = 1;
        int    prev_gear      = 1;       // last forward gear used for shift-event detection
        double gear_hold_timer = 0.0;
        double shift_event_timer = 0.0;  // remaining seconds in shift event window
        int    shift_direction = 0;      // +1=upshift, -1=downshift, 0=none
        int    reported_direction = 1;   // -1=R, 0=N, +1=D (sticky across standstill)
        double neutral_hold_timer = 0.0; // remaining N-hold during D<->R transition
        bool   initialized    = false;
    };

    struct PedalParams
    {
        double mass_kg            = 1500.0;
        double drag_coeff         = 0.30;
        double frontal_area_m2    = 2.3;
        double air_density        = 1.225;
        double rolling_resistance = 0.011;
        double engine_brake_decel = 1.5;
        std::vector<double> engine_brake_gear_factor = {1.6, 1.3, 1.1, 1.0, 0.85, 0.7};
    };

    struct ShiftParams
    {
        // Defaults model the 11th-gen Civic Hatchback Sport Touring (FL1) 6MT
        // with the L15C7 1.5L turbo (180hp / 240Nm). The internal gear set is
        // shared with the Civic Si; only the final drive differs (4.105 here
        // vs 4.353 on the Si).
        std::vector<double> gear_ratios       = {3.642, 2.080, 1.361, 1.024, 0.830, 0.686};
        double              final_drive_ratio = 4.105;
        std::vector<double> shift_up_kmh      = {20, 35, 54, 72, 88};
        std::vector<double> shift_down_kmh    = {14, 22, 36, 54, 72};
        double              kickdown_gain     = 0.35;
        double              brake_downshift_threshold = 0.4;
        double              min_gear_hold_s   = 0.5;
        double              rpm_tau_s         = 0.2;
        double              v_lockup_mps      = 5.0;

        // Shift-event modeling (transient behavior during a gear change)
        double              shift_event_duration_s = 0.18;  // event window length [s]
        double              upshift_dip_rpm        = 200.0; // RPM offset added during upshift dip (subtracted from target)
        double              downshift_blip_rpm     = 0.0;   // RPM offset added during downshift rev-match blip
        double              shift_torque_factor    = 0.3;   // torque scaling during event (0..1)
        double              rpm_tau_up_s           = 0.18;  // RPM lag during upshift event
        double              rpm_tau_down_s         = 0.25;  // RPM lag during downshift event
    };

    std::map<int, VehicleCache> cache_;
    PedalParams pedal_params_;
    std::map<std::string, ShiftParams> mode_shift_params_;
    std::string active_mode_ = "comfort";
    bool        params_loaded_ = false;

    const ShiftParams& Active() const;
    ShiftParams&       Active();

    // Physics constants (defaults retained for fields not in JSON)
    static constexpr double kIdleRPM        = 700.0;
    static constexpr double kMaxRPM         = 6500.0;
    static constexpr double kWheelRadius    = 0.32;   // [m]
    static constexpr double kDefaultMaxAcc  = 4.0;    // [m/s^2]
    static constexpr double kDefaultMaxDec  = 10.0;   // [m/s^2]
    static constexpr double kTorquePeakPos  = 0.65;
    static constexpr double kTorqueMin      = 0.3;
    static constexpr double kSpeedThreshold = 0.01;
    static constexpr double kPreviewTime    = 0.8;
    static constexpr double kPreviewDistMin = 2.0;
    static constexpr double kPreviewDistMax = 30.0;
    static constexpr double kSteerEmaAlpha  = 0.5;
    static constexpr double kPedalSmoothAlpha = 0.3;
    static constexpr double kGravity        = 9.81;
    static constexpr double kNeutralTransitionHold = 0.3;  // [s] N held during D<->R reversal

    int  SeedInitialGear(double speed_kmh) const;
    int  SelectGear(double speed_kmh, double throttle, double brake,
                    VehicleCache& vc, double dt) const;
    double EstimateRPM(double abs_speed, int gear, double dt, VehicleCache& vc) const;
    double EstimateTorque(double rpm) const;
    static int BuildLightMaskForObject(scenarioengine::Object* obj);
};

} // namespace gt_esmini
