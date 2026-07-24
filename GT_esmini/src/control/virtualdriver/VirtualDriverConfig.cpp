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
    {"control_point_offset", &VirtualDriverConfig::control_point_offset},
    {"control_point_min_speed", &VirtualDriverConfig::control_point_min_speed},
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
    {"sign_lookahead", &VirtualDriverConfig::sign_lookahead},
    {"stop_hold_time", &VirtualDriverConfig::stop_hold_time},
    {"stop_detect_speed", &VirtualDriverConfig::stop_detect_speed},
    {"stop_line_tol", &VirtualDriverConfig::stop_line_tol},
    {"creep_speed", &VirtualDriverConfig::creep_speed},
    {"creep_advance", &VirtualDriverConfig::creep_advance},
    {"yield_creep_speed", &VirtualDriverConfig::yield_creep_speed},
    {"sign_stop_margin", &VirtualDriverConfig::sign_stop_margin},
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
    {"crosswalk_yield_to_waiting", &VirtualDriverConfig::crosswalk_yield_to_waiting},
    {"crosswalk_ped_signal_aware", &VirtualDriverConfig::crosswalk_ped_signal_aware},
    {"override_enabled", &VirtualDriverConfig::override_enabled},
    {"override_button", &VirtualDriverConfig::override_button},
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
