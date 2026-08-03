#include "gt_esmini/control/virtualdriver/VirtualDriverConfig.hpp"
#include "gt_esmini/common/SimpleJson.hpp"
#include "logger.hpp"

#include <string>

namespace gt_esmini
{
namespace
{
struct DoubleField
{
    const char* key;
    double VirtualDriverConfig::*member;
};

struct IntField
{
    const char* key;
    int VirtualDriverConfig::*member;
};

struct BoolField
{
    const char* key;
    bool VirtualDriverConfig::*member;
};

struct StringField
{
    const char* key;
    std::string VirtualDriverConfig::*member;
};

const StringField kStringFields[] = {
    {"vehicle_params_file", &VirtualDriverConfig::vehicle_params_file},
    {"override_lateral", &VirtualDriverConfig::override_lateral},
    {"override_longitudinal", &VirtualDriverConfig::override_longitudinal},
    {"input_type", &VirtualDriverConfig::input_type},
    {"input_transport", &VirtualDriverConfig::input_transport},
};

const DoubleField kDoubleFields[] = {
    {"horizon_s", &VirtualDriverConfig::horizon_s},
    {"short_dt", &VirtualDriverConfig::short_dt},
    {"max_lateral_accel", &VirtualDriverConfig::max_lateral_accel},
    {"comfort_decel", &VirtualDriverConfig::comfort_decel},
    {"emergency_decel", &VirtualDriverConfig::emergency_decel},
    {"comfort_jerk", &VirtualDriverConfig::comfort_jerk},
    {"scan_distance", &VirtualDriverConfig::scan_distance},
    {"scan_step", &VirtualDriverConfig::scan_step},
    {"turn_speed", &VirtualDriverConfig::turn_speed},
    {"min_turn_speed", &VirtualDriverConfig::min_turn_speed},
    {"stop_band", &VirtualDriverConfig::stop_band},
    {"lookahead_gain", &VirtualDriverConfig::lookahead_gain},
    {"min_lookahead", &VirtualDriverConfig::min_lookahead},
    {"max_lookahead", &VirtualDriverConfig::max_lookahead},
    {"max_steer_angle", &VirtualDriverConfig::max_steer_angle},
    {"steering_sign", &VirtualDriverConfig::steering_sign},
    {"speed_kp", &VirtualDriverConfig::speed_kp},
    {"speed_ki", &VirtualDriverConfig::speed_ki},
    {"speed_kd", &VirtualDriverConfig::speed_kd},
    // AD steering safety envelope (feature:F7).
    {"a_lat_max_steer", &VirtualDriverConfig::a_lat_max_steer},
    {"yaw_rate_max", &VirtualDriverConfig::yaw_rate_max},
    {"steer_rate_max", &VirtualDriverConfig::steer_rate_max},
    {"envelope_v_floor", &VirtualDriverConfig::envelope_v_floor},
    {"ad_steering_envelope_steer_jerk_max", &VirtualDriverConfig::ad_steering_envelope_steer_jerk_max},
    // AD resume-merge trajectory (feature:F7).
    {"resume_merge_a_lat_comfort", &VirtualDriverConfig::resume_merge_a_lat_comfort},
    {"resume_merge_duration_min_s", &VirtualDriverConfig::resume_merge_duration_min_s},
    {"resume_merge_duration_max_s", &VirtualDriverConfig::resume_merge_duration_max_s},
    {"resume_merge_min_offset_m", &VirtualDriverConfig::resume_merge_min_offset_m},
    // AD lane-change initiation (vd-func:FUNC-055).
    {"lane_change_lead_time_s", &VirtualDriverConfig::lane_change_lead_time_s},
    {"lane_change_min_lead_distance_m", &VirtualDriverConfig::lane_change_min_lead_distance_m},
    {"lane_change_reserve_distance_m", &VirtualDriverConfig::lane_change_reserve_distance_m},
    {"lane_change_gap_min_m", &VirtualDriverConfig::lane_change_gap_min_m},
    {"lane_change_gap_headway_lead_s", &VirtualDriverConfig::lane_change_gap_headway_lead_s},
    {"lane_change_gap_headway_rear_s", &VirtualDriverConfig::lane_change_gap_headway_rear_s},
    {"lane_change_gap_ttc_min_s", &VirtualDriverConfig::lane_change_gap_ttc_min_s},
    {"lane_change_lateral_accel_comfort", &VirtualDriverConfig::lane_change_lateral_accel_comfort},
    {"lane_change_indicator_lead_time_s", &VirtualDriverConfig::lane_change_indicator_lead_time_s},
    // AD overtake maneuver (vd-func:FUNC-056). Only 5 new keys -- see design doc section 8.
    {"overtake_max_pass_time_s", &VirtualDriverConfig::overtake_max_pass_time_s},
    {"overtake_oncoming_lookahead_m", &VirtualDriverConfig::overtake_oncoming_lookahead_m},
    {"overtake_oncoming_safety_factor", &VirtualDriverConfig::overtake_oncoming_safety_factor},
    {"control_point_offset", &VirtualDriverConfig::control_point_offset},
    {"control_point_min_speed", &VirtualDriverConfig::control_point_min_speed},
    {"indicator_min_distance_m", &VirtualDriverConfig::indicator_min_distance_m},
    {"indicator_lead_time", &VirtualDriverConfig::indicator_lead_time},
    {"indicator_min_on_time", &VirtualDriverConfig::indicator_min_on_time},
    {"idm_time_headway", &VirtualDriverConfig::idm_time_headway},
    {"idm_min_gap", &VirtualDriverConfig::idm_min_gap},
    {"idm_max_accel", &VirtualDriverConfig::idm_max_accel},
    {"idm_comfort_decel", &VirtualDriverConfig::idm_comfort_decel},
    {"idm_desired_speed", &VirtualDriverConfig::idm_desired_speed},
    {"idm_lookahead", &VirtualDriverConfig::idm_lookahead},
    {"idm_lateral_tol", &VirtualDriverConfig::idm_lateral_tol},
    {"idm_target_horizon", &VirtualDriverConfig::idm_target_horizon},
    {"tl_lookahead", &VirtualDriverConfig::tl_lookahead},
    {"tl_yellow_decel", &VirtualDriverConfig::tl_yellow_decel},
    {"tl_stop_margin", &VirtualDriverConfig::tl_stop_margin},
    {"tl_junction_clearance", &VirtualDriverConfig::tl_junction_clearance},
    {"tl_stop_line_window", &VirtualDriverConfig::tl_stop_line_window},
    {"sign_lookahead", &VirtualDriverConfig::sign_lookahead},
    {"stop_hold_time", &VirtualDriverConfig::stop_hold_time},
    {"stop_detect_speed", &VirtualDriverConfig::stop_detect_speed},
    {"stop_line_tol", &VirtualDriverConfig::stop_line_tol},
    {"creep_speed", &VirtualDriverConfig::creep_speed},
    {"creep_advance", &VirtualDriverConfig::creep_advance},
    {"yield_creep_speed", &VirtualDriverConfig::yield_creep_speed},
    {"sign_stop_margin", &VirtualDriverConfig::sign_stop_margin},
    {"sign_stop_line_window", &VirtualDriverConfig::sign_stop_line_window},
    {"conflict_lookahead", &VirtualDriverConfig::conflict_lookahead},
    {"conflict_step", &VirtualDriverConfig::conflict_step},
    {"conflict_lane_margin", &VirtualDriverConfig::conflict_lane_margin},
    {"conflict_standoff", &VirtualDriverConfig::conflict_standoff},
    {"conflict_release_buffer", &VirtualDriverConfig::conflict_release_buffer},
    {"conflict_pet", &VirtualDriverConfig::conflict_pet},
    {"conflict_nominal_speed", &VirtualDriverConfig::conflict_nominal_speed},
    {"conflict_min_cross_angle_deg", &VirtualDriverConfig::conflict_min_cross_angle_deg},
    {"conflict_other_min_speed", &VirtualDriverConfig::conflict_other_min_speed},
    {"conflict_area_eps", &VirtualDriverConfig::conflict_area_eps},
    {"crosswalk_lookahead", &VirtualDriverConfig::crosswalk_lookahead},
    {"crosswalk_step", &VirtualDriverConfig::crosswalk_step},
    {"crosswalk_standoff", &VirtualDriverConfig::crosswalk_standoff},
    {"crosswalk_wait_margin", &VirtualDriverConfig::crosswalk_wait_margin},
    {"crosswalk_signal_link_radius", &VirtualDriverConfig::crosswalk_signal_link_radius},
    {"crosswalk_release_lateral_margin", &VirtualDriverConfig::crosswalk_release_lateral_margin},
    {"aeb_ttc_threshold", &VirtualDriverConfig::aeb_ttc_threshold},
    {"aeb_lateral_tol", &VirtualDriverConfig::aeb_lateral_tol},
    {"aeb_min_a_req", &VirtualDriverConfig::aeb_min_a_req},
    {"aeb_stop_margin", &VirtualDriverConfig::aeb_stop_margin},
    {"steering_threshold", &VirtualDriverConfig::steering_threshold},
    {"throttle_threshold", &VirtualDriverConfig::throttle_threshold},
    {"brake_threshold", &VirtualDriverConfig::brake_threshold},
    {"auto_return_timeout", &VirtualDriverConfig::auto_return_timeout},
    // F7b FFB target-track (flat keys; propagated into io_config_.ffb.target_track in the controller).
    {"ffb_target_track_kp",                              &VirtualDriverConfig::ffb_target_track_kp},
    {"ffb_target_track_kd",                              &VirtualDriverConfig::ffb_target_track_kd},
    {"ffb_target_track_max_force",                       &VirtualDriverConfig::ffb_target_track_max_force},
    {"ffb_target_track_hard_stop_zone",                  &VirtualDriverConfig::ffb_target_track_hard_stop_zone},
    {"ffb_target_track_friction_ff",                     &VirtualDriverConfig::ffb_target_track_friction_ff},
    {"ffb_target_track_friction_ff_eps",                 &VirtualDriverConfig::ffb_target_track_friction_ff_eps},
    {"ffb_target_track_feel_ratio",                      &VirtualDriverConfig::ffb_target_track_feel_ratio},
    {"ffb_target_track_override_steer_force_threshold",  &VirtualDriverConfig::ffb_target_track_override_steer_force_threshold},
    {"ffb_target_track_override_steer_dev_threshold",    &VirtualDriverConfig::ffb_target_track_override_steer_dev_threshold},
    {"ffb_target_track_override_sustain_time",           &VirtualDriverConfig::ffb_target_track_override_sustain_time},
    {"ffb_target_track_override_target_rate_gate",       &VirtualDriverConfig::ffb_target_track_override_target_rate_gate},
    {"ffb_target_track_override_position_error_rate_gate", &VirtualDriverConfig::ffb_target_track_override_position_error_rate_gate},
    {"ffb_target_track_override_residual_threshold",       &VirtualDriverConfig::ffb_target_track_override_residual_threshold},
    {"ffb_target_track_override_residual_reanchor_tau",    &VirtualDriverConfig::ffb_target_track_override_residual_reanchor_tau},
    {"ffb_target_track_override_shadow_breakaway",         &VirtualDriverConfig::ffb_target_track_override_shadow_breakaway},
    {"ffb_target_track_override_shadow_breakaway_left",     &VirtualDriverConfig::ffb_target_track_override_shadow_breakaway_left},
    {"ffb_target_track_override_shadow_breakaway_right",    &VirtualDriverConfig::ffb_target_track_override_shadow_breakaway_right},
    {"ffb_target_track_override_shadow_motion_epsilon",    &VirtualDriverConfig::ffb_target_track_override_shadow_motion_epsilon},
    {"ffb_target_track_override_shadow_kinetic",           &VirtualDriverConfig::ffb_target_track_override_shadow_kinetic},
    {"ffb_target_track_override_shadow_force_to_velocity", &VirtualDriverConfig::ffb_target_track_override_shadow_force_to_velocity},
    {"ffb_target_track_override_shadow_v_max",             &VirtualDriverConfig::ffb_target_track_override_shadow_v_max},
    {"ffb_target_track_override_shadow_velocity_tau",      &VirtualDriverConfig::ffb_target_track_override_shadow_velocity_tau},
    {"ffb_target_track_override_shadow_dead_time",         &VirtualDriverConfig::ffb_target_track_override_shadow_dead_time},
    {"ffb_target_track_override_shadow_onset_grace",       &VirtualDriverConfig::ffb_target_track_override_shadow_onset_grace},
    {"ffb_target_track_override_shadow_motion_rate_eps",   &VirtualDriverConfig::ffb_target_track_override_shadow_motion_rate_eps},
    {"ffb_safety_max_saturation_seconds",                  &VirtualDriverConfig::ffb_safety_max_saturation_seconds},
    {"ffb_safety_max_runtime_seconds",                     &VirtualDriverConfig::ffb_safety_max_runtime_seconds},
    {"ffb_safety_saturation_ratio",                        &VirtualDriverConfig::ffb_safety_saturation_ratio},
};

const BoolField kBoolFields[] = {
    {"respect_speed_limit", &VirtualDriverConfig::respect_speed_limit},
    {"policy_lead_enabled", &VirtualDriverConfig::policy_lead_enabled},
    {"policy_traffic_light_enabled", &VirtualDriverConfig::policy_traffic_light_enabled},
    {"policy_stop_yield_enabled", &VirtualDriverConfig::policy_stop_yield_enabled},
    {"policy_conflict_enabled", &VirtualDriverConfig::policy_conflict_enabled},
    {"policy_crosswalk_enabled", &VirtualDriverConfig::policy_crosswalk_enabled},
    {"policy_junction_priority_enabled", &VirtualDriverConfig::policy_junction_priority_enabled},
    {"policy_aeb_enabled", &VirtualDriverConfig::policy_aeb_enabled},
    {"tl_junction_guard_enabled", &VirtualDriverConfig::tl_junction_guard_enabled},
    {"tl_stop_line_aware_enabled", &VirtualDriverConfig::tl_stop_line_aware_enabled},
    {"sign_stop_line_aware_enabled", &VirtualDriverConfig::sign_stop_line_aware_enabled},
    {"crosswalk_yield_to_waiting", &VirtualDriverConfig::crosswalk_yield_to_waiting},
    {"crosswalk_ped_signal_aware", &VirtualDriverConfig::crosswalk_ped_signal_aware},
    {"override_enabled", &VirtualDriverConfig::override_enabled},
    {"override_button", &VirtualDriverConfig::override_button},
    {"override_button_takeover", &VirtualDriverConfig::override_button_takeover},
    {"ffb_target_track_enabled", &VirtualDriverConfig::ffb_target_track_enabled},   // F7b
    {"ad_steering_envelope_enabled", &VirtualDriverConfig::ad_steering_envelope_enabled},  // feature:F7
    {"resume_merge_enabled", &VirtualDriverConfig::resume_merge_enabled},  // feature:F7
    {"lane_change_initiation_enabled", &VirtualDriverConfig::lane_change_initiation_enabled},  // vd-func:FUNC-055
    {"overtake_enabled", &VirtualDriverConfig::overtake_enabled},  // vd-func:FUNC-056
    {"overtake_use_opposing_lane_enabled", &VirtualDriverConfig::overtake_use_opposing_lane_enabled},  // vd-func:FUNC-056
};

const IntField kIntFields[] = {
    {"input_port", &VirtualDriverConfig::input_port},
    // SDL2 wheel button bindings (only used when input_type == "sdl2_wheel").
    {"sdl2_override_button",        &VirtualDriverConfig::sdl2_override_button},
    {"sdl2_indicator_left_button",  &VirtualDriverConfig::sdl2_indicator_left_button},
    {"sdl2_indicator_right_button", &VirtualDriverConfig::sdl2_indicator_right_button},
    {"sdl2_upshift_button",         &VirtualDriverConfig::sdl2_upshift_button},
    {"sdl2_downshift_button",       &VirtualDriverConfig::sdl2_downshift_button},
    {"sdl2_headlight_button",       &VirtualDriverConfig::sdl2_headlight_button},
    {"sdl2_high_beam_button",       &VirtualDriverConfig::sdl2_high_beam_button},
    {"sdl2_fog_light_button",       &VirtualDriverConfig::sdl2_fog_light_button},
    {"sdl2_hazard_button",          &VirtualDriverConfig::sdl2_hazard_button},
    {"sdl2_auto_resume_button",     &VirtualDriverConfig::sdl2_auto_resume_button},  // feature:F7
};

void WarnIfWrongType(const simplejson::Value& root, const char* key, const char* expected_type)
{
    if (!root.Find(key)) return;
    LOG_WARN("VirtualDriverConfig: Ignoring '{}' because it is not {}", key, expected_type);
}
}  // namespace

bool VirtualDriverConfig::LoadFromFile(const std::string& filepath)
{
    LOG_INFO("VirtualDriverConfig: Loading from '{}'", filepath);

    simplejson::Value root;
    std::string error;
    if (!simplejson::LoadFile(filepath, root, &error))
    {
        LOG_WARN("VirtualDriverConfig: Failed to load '{}': {} — continuing with ALL built-in defaults",
                 filepath, error);
        return false;
    }
    if (!root.IsObject())
    {
        LOG_WARN("VirtualDriverConfig: Root JSON value in '{}' is not an object", filepath);
        return false;
    }

    for (const auto& field : kStringFields)
    {
        std::string parsed;
        if (root.GetString(field.key, parsed)) (this->*(field.member)) = parsed;
        else WarnIfWrongType(root, field.key, "a string");
    }
    for (const auto& field : kDoubleFields)
    {
        double parsed = 0.0;
        if (root.GetDouble(field.key, parsed)) (this->*(field.member)) = parsed;
        else WarnIfWrongType(root, field.key, "a number");
    }
    for (const auto& field : kBoolFields)
    {
        bool parsed = false;
        if (root.GetBool(field.key, parsed)) (this->*(field.member)) = parsed;
        else WarnIfWrongType(root, field.key, "a boolean");
    }
    for (const auto& field : kIntFields)
    {
        int parsed = 0;
        if (root.GetInt(field.key, parsed)) (this->*(field.member)) = parsed;
        else WarnIfWrongType(root, field.key, "an integer");
    }

    LOG_INFO("VirtualDriverConfig: planner(horizon={:.1f}s dt={:.2f}) driver(la_gain={:.2f} kp={:.2f}) input={}",
             horizon_s, short_dt, lookahead_gain, speed_kp, input_type);
    // Observable for feature:F7 GUI/runtime-reload verification: the parsed
    // SDL2 wheel button IDs. Only meaningful when input_type=="sdl2_wheel",
    // but always logged so a config edit can be confirmed to have taken
    // effect on the next scenario load without a rebuild.
    LOG_INFO("VirtualDriverConfig: sdl2 buttons: override={} resume={} indL={} indR={} up={} down={} hl={} hb={} fog={} hzd={}",
             sdl2_override_button, sdl2_auto_resume_button,
             sdl2_indicator_left_button, sdl2_indicator_right_button,
             sdl2_upshift_button, sdl2_downshift_button,
             sdl2_headlight_button, sdl2_high_beam_button,
             sdl2_fog_light_button, sdl2_hazard_button);
    return true;
}

PhysicsInitParams VirtualDriverConfig::PhysicsParams() const
{
    PhysicsInitParams p;
    p.vehicle_params_file = vehicle_params_file;
    return p;
}

TrajectoryShortPlannerConfig VirtualDriverConfig::ShortPlannerConfig() const
{
    return TrajectoryShortPlannerConfig{};  // defaults; horizon/dt passed per-frame via ShortPlanContext
}

ManeuverAwareSpeedPlannerConfig VirtualDriverConfig::MidLongConfig() const
{
    ManeuverAwareSpeedPlannerConfig c;
    c.max_lateral_accel   = max_lateral_accel;
    c.comfort_decel       = comfort_decel;
    c.emergency_decel     = emergency_decel;
    c.comfort_jerk        = comfort_jerk;
    c.scan_step           = scan_step;
    c.min_speed           = min_turn_speed;
    c.turn_speed          = turn_speed;
    c.stop_band           = stop_band;
    c.respect_speed_limit = respect_speed_limit;
    return c;
}

PIDPurePursuitConfig VirtualDriverConfig::DriverConfig() const
{
    PIDPurePursuitConfig c;
    c.lookahead_gain  = lookahead_gain;
    c.min_lookahead   = min_lookahead;
    c.max_lookahead   = max_lookahead;
    c.max_steer_angle = max_steer_angle;
    c.steering_sign   = steering_sign;
    c.kp = speed_kp;
    c.ki = speed_ki;
    c.kd = speed_kd;
    return c;
}

AdSteeringEnvelopeConfig VirtualDriverConfig::AdEnvelopeConfig() const
{
    AdSteeringEnvelopeConfig c;
    c.enabled         = ad_steering_envelope_enabled;
    c.a_lat_max_steer = a_lat_max_steer;
    c.yaw_rate_max    = yaw_rate_max;
    c.steer_rate_max  = steer_rate_max;
    c.v_floor         = envelope_v_floor;
    c.steer_jerk_max  = ad_steering_envelope_steer_jerk_max;
    return c;
}

ResumeMergeConfig VirtualDriverConfig::ResumeMergeCfg() const
{
    ResumeMergeConfig c;
    c.enabled        = resume_merge_enabled;
    c.a_lat_comfort  = resume_merge_a_lat_comfort;
    c.duration_min_s = resume_merge_duration_min_s;
    c.duration_max_s = resume_merge_duration_max_s;
    c.min_offset_m   = resume_merge_min_offset_m;
    return c;
}

LaneChangeInitiationConfig VirtualDriverConfig::LaneChangeInitiationCfg() const
{
    LaneChangeInitiationConfig c;
    c.enabled             = lane_change_initiation_enabled;
    c.lead_time_s         = lane_change_lead_time_s;
    c.min_lead_distance_m = lane_change_min_lead_distance_m;
    c.reserve_distance_m  = lane_change_reserve_distance_m;
    c.gap_min_m           = lane_change_gap_min_m;
    c.gap_headway_lead_s  = lane_change_gap_headway_lead_s;
    c.gap_headway_rear_s  = lane_change_gap_headway_rear_s;
    c.gap_ttc_min_s       = lane_change_gap_ttc_min_s;
    c.indicator_lead_time_s = lane_change_indicator_lead_time_s;
    return c;
}

ResumeMergeConfig VirtualDriverConfig::LaneChangeMergeCfg() const
{
    ResumeMergeConfig c;
    c.enabled        = true;  // outer gate is lane_change_initiation_enabled (see header doc)
    c.a_lat_comfort  = lane_change_lateral_accel_comfort;
    c.duration_min_s = kResumeMergeDefaultDurationMinS;  // reuse resume-merge's OWN defaults, not
    c.duration_max_s = kResumeMergeDefaultDurationMaxS;  // resume_merge_duration_*/min_offset_m --
    c.min_offset_m   = kResumeMergeDefaultMinOffsetM;    // the two features stay independently tunable
    return c;
}

OvertakeConfig VirtualDriverConfig::OvertakeCfg() const
{
    OvertakeConfig c;
    c.enabled                    = overtake_enabled;
    c.use_opposing_lane_enabled  = overtake_use_opposing_lane_enabled;
    c.max_pass_time_s            = overtake_max_pass_time_s;
    c.oncoming_lookahead_m       = overtake_oncoming_lookahead_m;
    c.oncoming_safety_factor     = overtake_oncoming_safety_factor;
    return c;
}

AutoIndicatorConfig VirtualDriverConfig::IndicatorConfig() const
{
    AutoIndicatorConfig c;
    c.lead_time   = indicator_lead_time;
    c.min_on_time = indicator_min_on_time;
    return c;
}

LeadVehicleAwareConfig VirtualDriverConfig::LeadConfig() const
{
    LeadVehicleAwareConfig c;
    c.idm.time_headway  = idm_time_headway;
    c.idm.min_gap       = idm_min_gap;
    c.idm.max_accel     = idm_max_accel;
    c.idm.comfort_decel = idm_comfort_decel;
    c.idm.desired_speed = idm_desired_speed;
    c.lookahead         = idm_lookahead;
    c.lateral_tol       = idm_lateral_tol;
    c.target_horizon    = idm_target_horizon;
    return c;
}

TrafficLightAwareConfig VirtualDriverConfig::TrafficLightConfig() const
{
    TrafficLightAwareConfig c;
    c.params.yellow_decel = tl_yellow_decel;
    c.lookahead           = tl_lookahead;
    c.stop_margin         = tl_stop_margin;

    c.junction_guard_enabled = tl_junction_guard_enabled;
    // Pulling a stop back to before a junction is still "halt at the line", so it
    // reuses tl_stop_margin rather than inventing a second standoff; the
    // feasibility test uses the planner's comfort_decel, which is the
    // deceleration that will actually shape the approach (see the header).
    c.junction.stop_margin    = tl_stop_margin;
    c.junction.exit_clearance = tl_junction_clearance;
    c.junction.decel          = comfort_decel;

    c.stop_line_aware_enabled = tl_stop_line_aware_enabled;
    c.stop_line_window        = tl_stop_line_window;
    return c;
}

StopYieldSignAwareConfig VirtualDriverConfig::StopYieldConfig() const
{
    StopYieldSignAwareConfig c;
    c.stop.stop_hold_time    = stop_hold_time;
    c.stop.stop_detect_speed = stop_detect_speed;
    c.stop.stop_line_tol     = stop_line_tol;
    c.stop.creep_speed       = creep_speed;
    c.stop.creep_advance     = creep_advance;
    c.yield_creep_speed      = yield_creep_speed;
    c.lookahead              = sign_lookahead;
    c.stop_margin            = sign_stop_margin;

    c.stop_line_aware_enabled = sign_stop_line_aware_enabled;
    c.stop_line_window        = sign_stop_line_window;
    return c;
}

ConflictPointResolverConfig VirtualDriverConfig::ConflictConfig() const
{
    ConflictPointResolverConfig c;
    c.lookahead           = conflict_lookahead;
    c.step                = conflict_step;
    c.lane_margin         = conflict_lane_margin;
    c.standoff            = conflict_standoff;
    c.release_buffer      = conflict_release_buffer;
    c.pet                 = conflict_pet;
    c.nominal_speed       = conflict_nominal_speed;
    c.min_cross_angle_deg = conflict_min_cross_angle_deg;
    c.other_min_speed     = conflict_other_min_speed;
    c.area_eps            = conflict_area_eps;
    c.junction_priority_enabled = policy_junction_priority_enabled;  // F3
    return c;
}

CrosswalkPedestrianAwareConfig VirtualDriverConfig::CrosswalkConfig() const
{
    CrosswalkPedestrianAwareConfig c;
    c.lookahead              = crosswalk_lookahead;
    c.step                   = crosswalk_step;
    c.standoff               = crosswalk_standoff;
    c.wait_margin            = crosswalk_wait_margin;
    c.yield_to_waiting       = crosswalk_yield_to_waiting;
    c.ped_signal_aware       = crosswalk_ped_signal_aware;
    c.signal_link_radius     = crosswalk_signal_link_radius;
    c.release_lateral_margin = crosswalk_release_lateral_margin;
    return c;
}

AebSafetyConfig VirtualDriverConfig::AebConfig() const
{
    AebSafetyConfig c;
    c.ttc_threshold = aeb_ttc_threshold;
    c.lateral_tol   = aeb_lateral_tol;
    c.min_a_req     = aeb_min_a_req;
    c.stop_margin   = aeb_stop_margin;
    return c;
}

}  // namespace gt_esmini
