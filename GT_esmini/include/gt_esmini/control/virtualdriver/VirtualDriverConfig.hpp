#pragma once

#include <string>

#include "gt_esmini/control/virtualdriver/TrajectoryShortPlanner.hpp"
#include "gt_esmini/control/virtualdriver/ManeuverAwareSpeedPlanner.hpp"
#include "gt_esmini/control/virtualdriver/PIDPurePursuitDriver.hpp"
#include "gt_esmini/control/virtualdriver/AutoIndicatorPolicy.hpp"
#include "gt_esmini/control/virtualdriver/policies/LeadVehicleAware.hpp"
#include "gt_esmini/control/virtualdriver/policies/TrafficLightAware.hpp"
#include "gt_esmini/control/virtualdriver/policies/StopYieldSignAware.hpp"
#include "gt_esmini/control/virtualdriver/policies/ConflictPointResolver.hpp"
#include "gt_esmini/control/virtualdriver/policies/CrosswalkPedestrianAware.hpp"
#include "gt_esmini/control/virtualdriver/policies/AebSafety.hpp"
#include "gt_esmini/control/common/PhysicsInitParams.hpp"

namespace gt_esmini
{

// Runtime config for ControllerVirtualDriver (config/virtual_driver.json).
//
// Keys are flat and uniquely named for the shared SimpleJson loader. The
// input/override subsystems reuse ManualDrive's IInputSource + OverrideManager;
// ToManualDriveIO-style mapping is done in the controller from these fields.
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
    // AEB phase 1: deceleration used instead of comfort_decel to shape the
    // approach to a SAFETY-tier constraint (see PolicyConstraint::Tier). Only
    // takes effect when policy_aeb_enabled fires AebSafety.
    double emergency_decel   = 8.0;   // [m/s^2]
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
    bool   policy_conflict_enabled      = false;
    bool   policy_crosswalk_enabled     = false;
    // F3 — unsignalised-junction right-of-way. Layers onto the conflict resolver:
    // when ON, the ego does NOT yield to a crossing vehicle it OUT-RANKS via the
    // OpenDRIVE <priority high low> list (P5 side model); un-ranked / no-priority
    // junctions keep the base yield. Requires policy_conflict_enabled (the gate
    // lives inside ConflictPointResolver). Default OFF -> no behaviour change.
    bool   policy_junction_priority_enabled = false;
    // AEB (phase 1) — forward-collision emergency braking guardian. Longitudinal
    // only (no steering); fires a SAFETY-tier STOP_AT_S on a collision-course
    // gate (TTC + required-decel), independent of policy_lead_enabled so it
    // composes with (and can catch what) the comfort-tier lead follow misses.
    // Default OFF -> no behaviour change.
    bool   policy_aeb_enabled           = false;
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
    // 3d — conflict-corridor resolver (unsignalised crossing yield, space-time
    // occupancy of width-inflated path corridors; see ConflictPointResolver).
    double conflict_lookahead           = 120.0; // [m]   path prediction horizon (ego + others)
    double conflict_step                = 1.0;   // [m]   corridor sampling resolution (arc fidelity)
    double conflict_lane_margin         = 0.25;  // [m]   lateral safety added to each half-width
    double conflict_standoff            = 5.0;   // [m]   stop this far before the conflict-region entry
    double conflict_release_buffer      = 3.0;   // [m]   extra travel past the region exit before release
    double conflict_pet                 = 1.5;   // [s]   post-encroachment safety time
    double conflict_nominal_speed       = 5.0;   // [m/s] floor on v_ego for the arrival estimate (anti-chatter)
    double conflict_min_cross_angle_deg = 20.0;  // [deg] same-direction filter (reject near-parallel overlaps)
    double conflict_other_min_speed     = 0.5;   // [m/s] ignore (near-)stationary others not yet at their region
    double conflict_area_eps            = 0.10;  // [m^2] min clipped quad-pair area to call it a conflict (detection sensitivity)
    // 3d ext — crosswalk pedestrian yield (crossing rule = unconditional collision
    // avoidance; waiting rule = courtesy/law, signal-gatable). See CrosswalkPedestrianAware.
    double crosswalk_lookahead              = 80.0;  // [m]   route scan horizon
    double crosswalk_step                   = 1.0;   // [m]   scan sampling (fine enough for ~3-4 m wide crosswalks)
    double crosswalk_standoff               = 3.0;   // [m]   stop this far before the footprint entry
    double crosswalk_wait_margin            = 2.0;   // [m]   ped within this distance of the footprint (outside it) counts as waiting
    bool   crosswalk_yield_to_waiting       = true;  // JP-law default: yield to peds waiting to cross
    bool   crosswalk_ped_signal_aware       = true;  // gate the WAITING rule on a linked pedestrian signal
    double crosswalk_signal_link_radius     = 10.0;  // [m]   |signal_s - crosswalk_s| on the same road
    double crosswalk_release_lateral_margin = 0.5;   // [m]   passage band = ego half-width + this

    // AEB (phase 1) — forward-collision safety gate (see AebSafety). Longitudinal
    // guardian only. Fires a SAFETY-tier STOP_AT_S (approached at
    // emergency_decel, above) when v_close>0 && gap>0 && TTC<aeb_ttc_threshold
    // && a_req>aeb_min_a_req against the nearest admitted candidate ahead.
    double aeb_ttc_threshold  = 2.5;  // [s]     fire when predicted time-to-collision drops below this
    double aeb_lateral_tol    = 3.5;  // [m]     widened admission window (sees an in-progress cut-in, not just dLaneId==0)
    double aeb_min_a_req      = 3.0;  // [m/s^2] fire only when the required deceleration exceeds this
    double aeb_stop_margin    = 2.0;  // [m]     extra standoff behind the bumper-to-bumper gap for the emitted stop point

    // --- Override (maps to OverrideManager) ---
    bool        override_enabled       = true;
    bool        override_button        = true;
    double      steering_threshold     = 0.05;
    double      throttle_threshold     = 0.1;
    double      brake_threshold        = 0.1;
    double      auto_return_timeout    = 0.0;
    std::string override_lateral       = "manual";    // "manual" (overridable) | "scenario" (locked auto)
    std::string override_longitudinal  = "manual";

    // --- FFB target-track (F7b) — copied into io_config_.ffb.target_track ---
    // Flat keys (VD config style; the equivalent lives under ffb.target_track in
    // ManualDriveConfig). Defaults match scripts/ffb_spike/README.md §1e/§2e —
    // NORMALIZED axis-fraction units (not radians). enabled=false so existing
    // VD behavior is bit-identical unless opt-in.
    bool   ffb_target_track_enabled                              = false;
    double ffb_target_track_kp                                   = 4.0;
    double ffb_target_track_kd                                   = 0.35;
    double ffb_target_track_max_force                            = 0.6;
    double ffb_target_track_hard_stop_zone                       = 0.85;
    // Coulomb friction feed-forward (< min breakaway 0.170) + road-feel
    // authority while the servo is active. CHARACTERIZATION.md §4/§6/§7.
    double ffb_target_track_friction_ff                          = 0.15;
    double ffb_target_track_friction_ff_eps                      = 0.01;
    double ffb_target_track_feel_ratio                           = 0.0;
    double ffb_target_track_override_steer_force_threshold       = 0.20;
    double ffb_target_track_override_steer_dev_threshold         = 0.04;
    double ffb_target_track_override_sustain_time                = 0.10;   // seconds
    double ffb_target_track_override_target_rate_gate            = 0.30;   // axis-frac / s
    double ffb_target_track_override_position_error_rate_gate    = 0.10;   // axis-frac / s
    double ffb_target_track_override_wheel_over_target_epsilon = 0.05;   // axis-fraction; post-f723fa90

    // --- Input source (reuses ManualDrive IInputSource) ---
    std::string input_type = "stub";  // "stub" | "network" | "sdl2_wheel"
    int         input_port = 9100;
    std::string input_transport = "udp";

    // SDL2 wheel button IDs (only used when input_type=="sdl2_wheel"). Every
    // key ends up in io_config_.sdl2.* and drives SDL2WheelInput.Poll button
    // reads. Prefixed sdl2_* to avoid clashing with the bool `override_button`
    // above (which enables/disables the OVERRIDE bit dispatch, not a wheel
    // binding). Defaults mirror ManualDriveConfig::sdl2 so the wheel behaves
    // identically under VD without any JSON present. -1 = unassigned.
    // feature:F7 — sdl2_auto_resume_button is the new manual->auto RESUME.
    int         sdl2_override_button        = 0;
    int         sdl2_indicator_left_button  = 7;
    int         sdl2_indicator_right_button = 6;
    int         sdl2_upshift_button         = 4;
    int         sdl2_downshift_button       = 5;
    int         sdl2_headlight_button       = -1;
    int         sdl2_high_beam_button       = -1;
    int         sdl2_fog_light_button       = -1;
    int         sdl2_hazard_button          = -1;
    int         sdl2_auto_resume_button     = -1;

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
    ConflictPointResolverConfig    ConflictConfig() const;
    CrosswalkPedestrianAwareConfig CrosswalkConfig() const;
    AebSafetyConfig                AebConfig() const;
};

}  // namespace gt_esmini
