#pragma once

#include <string>

#include "gt_esmini/control/virtualdriver/TrajectoryShortPlanner.hpp"
#include "gt_esmini/control/virtualdriver/ManeuverAwareSpeedPlanner.hpp"
#include "gt_esmini/control/virtualdriver/PIDPurePursuitDriver.hpp"
#include "gt_esmini/control/virtualdriver/AutoIndicatorPolicy.hpp"
#include "gt_esmini/control/common/PhysicsInitParams.hpp"

namespace gt_esmini
{

// Runtime config for ControllerVirtualDriver (config/virtual_driver.json).
//
// Keys are flat and uniquely named so the line-based parser (mirroring
// ManualDriveConfig) cannot cross-match. The input/override subsystems reuse
// ManualDrive's IInputSource + OverrideManager; ToManualDriveIO-style mapping
// is done in the controller from these fields.
struct VirtualDriverConfig
{
    // --- Physics backend ---
    std::string vehicle_params_file = "real_vehicle_params.json";

    // --- Short planner ---
    double horizon_s = 3.0;
    double short_dt  = 0.1;

    // --- Mid/long planner (Phase 2: ManeuverAwareSpeedPlanner) ---
    double max_lateral_accel = 2.5;   // [m/s^2] curve speed = sqrt(a_lat/|kappa|)
    double comfort_decel     = 2.0;   // [m/s^2] backward-pass deceleration
    double comfort_jerk      = 1.5;   // [m/s^3] optional profile smoothing
    double scan_distance     = 300.0; // [m] route look-ahead for v_target(s)
    double scan_step         = 5.0;   // [m] forward scan resolution
    double turn_speed        = 5.0;   // [m/s] cap over junction connecting roads
    double min_turn_speed    = 2.0;   // [m/s] floor on any computed ceiling
    bool   respect_speed_limit = true;

    // --- Driver model (PID + Pure Pursuit) ---
    double lookahead_gain = 0.5;
    double min_lookahead  = 4.0;
    double max_lookahead  = 20.0;
    double max_steer_angle = 0.61;
    double steering_sign   = -1.0;
    double speed_kp = 0.6;
    double speed_ki = 0.2;
    double speed_kd = 0.0;

    // --- Indicator ---
    double indicator_lead_time   = 2.0;
    double indicator_min_on_time = 0.3;

    // --- Override (maps to OverrideManager) ---
    bool        override_enabled       = true;
    bool        override_button        = true;
    double      steering_threshold     = 0.05;
    double      throttle_threshold     = 0.1;
    double      brake_threshold        = 0.1;
    double      auto_return_timeout    = 0.0;
    std::string override_lateral       = "manual";    // "manual" (overridable) | "scenario" (locked auto)
    std::string override_longitudinal  = "manual";

    // --- Input source (reuses ManualDrive IInputSource) ---
    std::string input_type = "stub";  // "stub" | "network" | "sdl2_wheel"
    int         input_port = 9100;
    std::string input_transport = "udp";

    bool LoadFromFile(const std::string& filepath);

    // Convenience accessors for the sub-configs of the pluggable components.
    PhysicsInitParams              PhysicsParams() const;
    TrajectoryShortPlannerConfig   ShortPlannerConfig() const;
    ManeuverAwareSpeedPlannerConfig MidLongConfig() const;
    PIDPurePursuitConfig           DriverConfig() const;
    AutoIndicatorConfig            IndicatorConfig() const;
};

}  // namespace gt_esmini
