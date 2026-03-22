#include "gt_esmini/control/manualdrive/ManualDriveConfig.hpp"
#include "logger.hpp"

#include <fstream>
#include <string>

namespace gt_esmini
{

bool ManualDriveConfig::LoadFromFile(const std::string& filepath)
{
    LOG_INFO("ManualDriveConfig: Loading from '{}'", filepath);
    std::ifstream file(filepath);
    if (!file.is_open())
    {
        LOG_WARN("ManualDriveConfig: Failed to open '{}'", filepath);
        return false;
    }

    std::string line;
    while (std::getline(file, line))
    {
        // Match quoted JSON key exactly: "key" must appear as a whole token.
        // This prevents "sat_gain" from matching "sat_centering_gain".
        auto key_matches = [&](const std::string& key) -> bool {
            std::string quoted = "\"" + key + "\"";
            return line.find(quoted) != std::string::npos;
        };

        auto parse_string = [&](const std::string& key, std::string& val) {
            if (key_matches(key))
            {
                size_t colon = line.find(":");
                if (colon != std::string::npos)
                {
                    std::string raw = line.substr(colon + 1);
                    // Strip quotes, commas, whitespace
                    std::string result;
                    for (char c : raw)
                    {
                        if (c != '"' && c != ',' && c != ' ' && c != '\t' && c != '\r')
                        {
                            result += c;
                        }
                    }
                    if (!result.empty())
                    {
                        val = result;
                    }
                }
            }
        };

        auto parse_double = [&](const std::string& key, double& val) {
            if (key_matches(key))
            {
                size_t colon = line.find(":");
                if (colon != std::string::npos)
                {
                    try
                    {
                        val = std::stod(line.substr(colon + 1));
                    }
                    catch (...)
                    {
                    }
                }
            }
        };

        auto parse_int = [&](const std::string& key, int& val) {
            if (key_matches(key))
            {
                size_t colon = line.find(":");
                if (colon != std::string::npos)
                {
                    try
                    {
                        val = std::stoi(line.substr(colon + 1));
                    }
                    catch (...)
                    {
                    }
                }
            }
        };

        auto parse_bool = [&](const std::string& key, bool& val) {
            if (key_matches(key))
            {
                size_t colon = line.find(":");
                if (colon != std::string::npos)
                {
                    std::string raw = line.substr(colon + 1);
                    val = (raw.find("true") != std::string::npos || raw.find("1") != std::string::npos);
                }
            }
        };

        // Top-level
        parse_string("input_type", input_type);
        parse_string("physics_type", physics_type);
        parse_bool("ffb_enabled", ffb_enabled);

        // SDL2 input
        parse_int("device_index", sdl2.device_index);
        parse_double("deadzone", sdl2.deadzone);
        parse_int("upshift_button", sdl2.upshift_button);
        parse_int("downshift_button", sdl2.downshift_button);
        parse_int("override_button", sdl2.override_button);
        parse_int("indicator_left_button", sdl2.indicator_left_button);
        parse_int("indicator_right_button", sdl2.indicator_right_button);
        parse_int("headlight_button", sdl2.headlight_button);
        parse_int("high_beam_button", sdl2.high_beam_button);
        parse_int("fog_light_button", sdl2.fog_light_button);
        parse_int("hazard_button", sdl2.hazard_button);

        // Indicator auto-cancel
        parse_double("indicator_cancel_angle", indicator_cancel_angle);

        // Network input
        parse_string("transport_type", input_network.transport_type);
        parse_int("port", input_network.port);
        parse_string("level", input_network.level);

        // Physics: RealVehicle
        parse_string("vehicle_params_file", real_vehicle.vehicle_params_file);

        // Physics: Network
        parse_string("host", physics_network.host);
        parse_int("cmd_port", physics_network.cmd_port);
        parse_int("state_port", physics_network.state_port);

        // FFB (v5)
        parse_double("sat_gain", ffb.sat_gain);
        parse_double("sat_centering_gain", ffb.sat_centering_gain);
        parse_double("friction_base", ffb.friction_base);
        parse_double("friction_speed_gain", ffb.friction_speed_gain);
        parse_double("damper_base", ffb.damper_base);
        parse_double("damper_speed_gain", ffb.damper_speed_gain);
        parse_double("soft_stop_gain", ffb.soft_stop_gain);
        parse_double("lock_angle", ffb.lock_angle);
        parse_double("assist_low_speed", ffb.assist_low_speed);
        parse_double("assist_high_speed", ffb.assist_high_speed);
        parse_double("max_force", ffb.max_force);
        parse_bool("disable_non_realtime", ffb.disable_non_realtime);

        // Domain assignment
        parse_string("lateral", domain.lateral);
        parse_string("longitudinal", domain.longitudinal);

        // Override
        parse_bool("enabled", override_cfg.enabled);
        parse_double("steering_threshold", override_cfg.steering_threshold);
        parse_double("throttle_threshold", override_cfg.throttle_threshold);
        parse_double("brake_threshold", override_cfg.brake_threshold);
        parse_double("auto_return_timeout", override_cfg.auto_return_timeout);
        parse_bool("button_override", override_cfg.button_override);
    }

    LOG_INFO("ManualDriveConfig: Parsed FFB — sat_gain={:.3f} centering={:.3f} fric_base={:.3f} "
             "assist_lo={:.2f} assist_hi={:.2f} max_force={:.2f}",
             ffb.sat_gain, ffb.sat_centering_gain, ffb.friction_base,
             ffb.assist_low_speed, ffb.assist_high_speed, ffb.max_force);

    return true;
}

} // namespace gt_esmini
