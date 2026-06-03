#pragma once

#include <string>

#include "gt_esmini/control/virtualdriver/TrajectoryShortPlanner.hpp"
#include "gt_esmini/control/virtualdriver/ManeuverAwareSpeedPlanner.hpp"
#include "gt_esmini/control/virtualdriver/PIDPurePursuitDriver.hpp"
#include "gt_esmini/control/virtualdriver/AutoIndicatorPolicy.hpp"
#include "gt_esmini/control/virtualdriver/policies/LeadVehicleAware.hpp"
#include "gt_esmini/control/virtualdriver/policies/TrafficLightAware.hpp"
#include "gt_esmini/control/virtualdriver/policies/StopYieldSignAware.hpp"
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
    double max_lateral_accel = 2.0;   // [m/s^2] curve speed = sqrt(a_lat/|kappa|)
    double comfort_decel     = 2.0;   // [m/s^2] backward-pass deceleration
    double comfort_jerk      = 1.5;   // [m/s^3] jerk-limited profile smoothing
    double scan_distance     = 300.0; // [m] route look-ahead for v_target(s)
    double scan_step         = 2.0;   // [m] forward scan resolution
    double turn_speed        = 5.0;   // [m/s] cap over junction connecting roads
    double min_turn_speed    = 2.0;   // [m/s] floor on any computed ceiling
    double stop_band         = 2.0;   // [m] hard-zero band before a policy STOP point (firm stop)
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

    // --- Control point (P2 issue 2): lateral reference forward of the origin ---
    // Pure pursuit tracks the vehicle ORIGIN (≈ rear axle in esmini), so on a tight
    // turn the front bumper swings wide and leaves the lane. Shift the lateral
    // control point (and the matching preview lane-center anchor) forward toward the
    // front axle while driving forward. Meters ahead of the origin:
    //   > 0  explicit distance [m]
    //   = 0  AUTO — front-axle distance (wheel_base = length*0.6); enabled by default
    //   < 0  disabled — keep the origin (rear) reference (Phase 1 behavior)
    // Tune per vehicle; verify the front bumper stays in-lane on a junction turn.
    double control_point_offset    = 0.0;   // [m] forward; 0 = auto(wheel_base)
    double control_point_min_speed = 1.0;   // [m/s] below this, no shift (stop/reverse)

    // --- Indicator ---
    double indicator_lead_time   = 2.0;
    double indicator_min_on_time = 0.3;

    // --- Traffic policies (Phase 3) ---
    // On/off per policy. Default OFF so Phase 1/2 behavior is unchanged unless a
    // scenario opts in (keeps the non-regression smoke/anticipation checks valid).
    bool   policy_lead_enabled          = false;
    bool   policy_traffic_light_enabled = false;
    bool   policy_stop_yield_enabled    = false;
    // 3a — lead-vehicle IDM follow.
    double idm_time_headway   = 1.5;   // [s]
    double idm_min_gap        = 2.0;   // [m]
    double idm_max_accel      = 1.5;   // [m/s^2]
    double idm_comfort_decel  = 2.0;   // [m/s^2]
    double idm_desired_speed  = 50.0;  // [m/s] free-flow v0 (kept high; SpeedAction governs)
    double idm_lookahead      = 120.0; // [m]
    double idm_lateral_tol    = 2.0;   // [m]
    double idm_target_horizon = 0.5;   // [s] tau
    // 3b — traffic light.
    double tl_lookahead       = 80.0;  // [m]
    double tl_yellow_decel    = 4.0;   // [m/s^2] max decel accepted to stop on yellow
    double tl_stop_margin     = 3.0;   // [m] halt this far before the signal (front at line, stays in scan)
    // 3c — stop / yield sign.
    double sign_lookahead     = 80.0;  // [m]
    double stop_hold_time     = 1.5;   // [s] dwell once stopped
    double stop_detect_speed  = 0.3;   // [m/s] counts as stopped
    double stop_line_tol      = 2.0;   // [m] close enough to the line
    double creep_speed        = 2.0;   // [m/s] edge-forward speed cap
    double creep_advance      = 4.0;   // [m] how far past the line to creep
    double yield_creep_speed  = 3.0;   // [m/s] YIELD = decelerate only
    double sign_stop_margin   = 3.0;   // [m] halt this far before the sign (front at line, stays in scan)

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
    LeadVehicleAwareConfig         LeadConfig() const;
    TrafficLightAwareConfig        TrafficLightConfig() const;
    StopYieldSignAwareConfig       StopYieldConfig() const;
};

}  // namespace gt_esmini
