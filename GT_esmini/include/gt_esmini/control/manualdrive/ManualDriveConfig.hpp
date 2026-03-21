#pragma once

#include <string>

namespace gt_esmini
{

struct ManualDriveConfig
{
    // Top-level type selection
    std::string input_type   = "sdl2_wheel";     // "sdl2_wheel", "network", "stub"
    std::string physics_type = "real_vehicle";    // "real_vehicle", "network"
    bool        ffb_enabled  = true;

    // Input: SDL2 wheel
    struct
    {
        int    device_index          = 0;
        double deadzone              = 0.05;
        int    upshift_button        = 4;
        int    downshift_button      = 5;
        int    override_button       = 0;
        int    indicator_left_button = 7;
        int    indicator_right_button = 6;
        int    headlight_button     = -1;  // -1 = unassigned
        int    high_beam_button     = -1;
        int    fog_light_button     = -1;
        int    hazard_button        = -1;
    } sdl2;

    // Indicator auto-cancel
    double indicator_cancel_angle = 0.06;  // normalized (~20 deg)

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

    // Domain assignment (lateral / longitudinal)
    struct
    {
        std::string lateral      = "manual";  // "manual" or "scenario"
        std::string longitudinal = "manual";  // "manual" or "scenario"
    } domain;

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
