#include "gt_esmini/control/racingwheel/RacingWheelConfig.hpp"

#include <fstream>
#include <string>

namespace gt_esmini
{

bool RacingWheelConfig::LoadFromFile(const std::string& filepath)
{
    std::ifstream file(filepath);
    if (!file.is_open())
    {
        return false;
    }

    std::string line;
    while (std::getline(file, line))
    {
        auto parse_string = [&](const std::string& key, std::string& val) {
            if (line.find(key) != std::string::npos)
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
            if (line.find(key) != std::string::npos)
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
            if (line.find(key) != std::string::npos)
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
            if (line.find(key) != std::string::npos)
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

        // Network input
        parse_string("transport_type", input_network.transport_type);
        parse_int("\"port\"", input_network.port);
        parse_string("\"level\"", input_network.level);

        // Physics: RealVehicle
        parse_string("vehicle_params_file", real_vehicle.vehicle_params_file);

        // Physics: Network
        parse_string("\"host\"", physics_network.host);
        parse_int("cmd_port", physics_network.cmd_port);
        parse_int("state_port", physics_network.state_port);

        // FFB
        parse_double("spring_coefficient", ffb.spring_coefficient);
        parse_double("damper_coefficient", ffb.damper_coefficient);
        parse_double("constant_gain", ffb.constant_gain);
        parse_double("max_force", ffb.max_force);
        parse_bool("disable_non_realtime", ffb.disable_non_realtime);

        // Override
        parse_bool("\"enabled\"", override_cfg.enabled);
        parse_double("steering_threshold", override_cfg.steering_threshold);
        parse_double("throttle_threshold", override_cfg.throttle_threshold);
        parse_double("brake_threshold", override_cfg.brake_threshold);
        parse_double("auto_return_timeout", override_cfg.auto_return_timeout);
        parse_bool("button_override", override_cfg.button_override);
    }

    return true;
}

} // namespace gt_esmini
