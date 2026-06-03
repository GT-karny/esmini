#include "gt_esmini/control/virtualdriver/VirtualDriverConfig.hpp"
#include "logger.hpp"

#include <fstream>
#include <string>

namespace gt_esmini
{

bool VirtualDriverConfig::LoadFromFile(const std::string& filepath)
{
    LOG_INFO("VirtualDriverConfig: Loading from '{}'", filepath);
    std::ifstream file(filepath);
    if (!file.is_open())
    {
        LOG_WARN("VirtualDriverConfig: Failed to open '{}'", filepath);
        return false;
    }

    std::string line;
    while (std::getline(file, line))
    {
        auto key_matches = [&](const std::string& key) -> bool {
            return line.find("\"" + key + "\"") != std::string::npos;
        };
        auto parse_string = [&](const std::string& key, std::string& val) {
            if (!key_matches(key)) return;
            size_t colon = line.find(":");
            if (colon == std::string::npos) return;
            std::string result;
            for (char c : line.substr(colon + 1))
                if (c != '"' && c != ',' && c != ' ' && c != '\t' && c != '\r')
                    result += c;
            if (!result.empty()) val = result;
        };
        auto parse_double = [&](const std::string& key, double& val) {
            if (!key_matches(key)) return;
            size_t colon = line.find(":");
            if (colon == std::string::npos) return;
            try { val = std::stod(line.substr(colon + 1)); } catch (...) {}
        };
        auto parse_int = [&](const std::string& key, int& val) {
            if (!key_matches(key)) return;
            size_t colon = line.find(":");
            if (colon == std::string::npos) return;
            try { val = std::stoi(line.substr(colon + 1)); } catch (...) {}
        };
        auto parse_bool = [&](const std::string& key, bool& val) {
            if (!key_matches(key)) return;
            size_t colon = line.find(":");
            if (colon == std::string::npos) return;
            val = line.substr(colon + 1).find("true") != std::string::npos;
        };

        parse_string("vehicle_params_file", vehicle_params_file);

        parse_double("horizon_s", horizon_s);
        parse_double("short_dt", short_dt);

        parse_double("max_lateral_accel", max_lateral_accel);
        parse_double("comfort_decel", comfort_decel);
        parse_double("comfort_jerk", comfort_jerk);
        parse_double("scan_distance", scan_distance);
        parse_double("scan_step", scan_step);
        parse_double("turn_speed", turn_speed);
        parse_double("min_turn_speed", min_turn_speed);
        parse_double("stop_band", stop_band);
        parse_bool("respect_speed_limit", respect_speed_limit);

        parse_double("lookahead_gain", lookahead_gain);
        parse_double("min_lookahead", min_lookahead);
        parse_double("max_lookahead", max_lookahead);
        parse_double("max_steer_angle", max_steer_angle);
        parse_double("steering_sign", steering_sign);
        parse_double("speed_kp", speed_kp);
        parse_double("speed_ki", speed_ki);
        parse_double("speed_kd", speed_kd);

        parse_double("control_point_offset", control_point_offset);
        parse_double("control_point_min_speed", control_point_min_speed);

        parse_double("indicator_lead_time", indicator_lead_time);
        parse_double("indicator_min_on_time", indicator_min_on_time);

        parse_bool("policy_lead_enabled", policy_lead_enabled);
        parse_bool("policy_traffic_light_enabled", policy_traffic_light_enabled);
        parse_bool("policy_stop_yield_enabled", policy_stop_yield_enabled);
        parse_double("idm_time_headway", idm_time_headway);
        parse_double("idm_min_gap", idm_min_gap);
        parse_double("idm_max_accel", idm_max_accel);
        parse_double("idm_comfort_decel", idm_comfort_decel);
        parse_double("idm_desired_speed", idm_desired_speed);
        parse_double("idm_lookahead", idm_lookahead);
        parse_double("idm_lateral_tol", idm_lateral_tol);
        parse_double("idm_target_horizon", idm_target_horizon);
        parse_double("tl_lookahead", tl_lookahead);
        parse_double("tl_yellow_decel", tl_yellow_decel);
        parse_double("tl_stop_margin", tl_stop_margin);
        parse_double("sign_lookahead", sign_lookahead);
        parse_double("stop_hold_time", stop_hold_time);
        parse_double("stop_detect_speed", stop_detect_speed);
        parse_double("stop_line_tol", stop_line_tol);
        parse_double("creep_speed", creep_speed);
        parse_double("creep_advance", creep_advance);
        parse_double("yield_creep_speed", yield_creep_speed);
        parse_double("sign_stop_margin", sign_stop_margin);

        parse_bool("override_enabled", override_enabled);
        parse_bool("override_button", override_button);
        parse_double("steering_threshold", steering_threshold);
        parse_double("throttle_threshold", throttle_threshold);
        parse_double("brake_threshold", brake_threshold);
        parse_double("auto_return_timeout", auto_return_timeout);
        parse_string("override_lateral", override_lateral);
        parse_string("override_longitudinal", override_longitudinal);

        parse_string("input_type", input_type);
        parse_int("input_port", input_port);
        parse_string("input_transport", input_transport);
    }

    LOG_INFO("VirtualDriverConfig: planner(horizon={:.1f}s dt={:.2f}) driver(la_gain={:.2f} kp={:.2f}) input={}",
             horizon_s, short_dt, lookahead_gain, speed_kp, input_type);
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

}  // namespace gt_esmini
