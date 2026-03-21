#pragma once

#include <string>

namespace gt_esmini
{

struct RacingWheelConfig
{
    // Top-level type selection
    std::string input_type   = "stub";          // "sdl2_wheel", "network", "stub"
    std::string physics_type = "real_vehicle";   // "real_vehicle", "network"
    bool        ffb_enabled  = false;

    // Input: SDL2 wheel
    struct
    {
        int    device_index = 0;
        double deadzone     = 0.05;
    } sdl2;

    // Input: Network bridge
    struct
    {
        std::string transport_type = "udp";  // "udp", "tcp"
        int         port           = 9100;
        std::string level          = "pedal_steer";  // "pedal_steer", "motion_request"
    } input_network;

    // Physics: RealVehicle
    struct
    {
        std::string vehicle_params_file = "real_vehicle_params.json";
    } real_vehicle;

    // Physics: Network bridge (external simulator)
    struct
    {
        std::string transport_type = "udp";
        std::string host           = "127.0.0.1";
        int         cmd_port       = 9200;
        int         state_port     = 9201;
    } physics_network;

    // FFB parameters
    struct
    {
        double spring_coefficient = 0.5;
        double damper_coefficient = 0.3;
        double constant_gain      = 1.0;
        double max_force          = 1.0;
        bool   disable_non_realtime = true;
    } ffb;

    // Override (auto <-> manual)
    struct
    {
        bool   enabled             = true;
        double steering_threshold  = 0.05;
        double throttle_threshold  = 0.1;
        double brake_threshold     = 0.1;
        double auto_return_timeout = 0.0;  // 0 = no timeout
        bool   button_override     = true;
    } override_cfg;

    bool LoadFromFile(const std::string& filepath);
};

} // namespace gt_esmini
