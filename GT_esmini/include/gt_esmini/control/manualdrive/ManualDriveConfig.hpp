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
        int    auto_resume_button   = -1;  // feature:F7 — resume AD after manual override
    } sdl2;

    // Input: SDL2 keyboard
    // Key names are SDL scancode names ("A", "Space", "Left", "LShift", ...).
    // Empty string disables the binding.
    struct
    {
        std::string steer_left      = "A";
        std::string steer_right     = "D";
        std::string throttle        = "W";
        std::string brake           = "S";
        std::string clutch          = "LShift";
        std::string upshift         = "E";
        std::string downshift       = "Q";
        std::string override_key    = "O";
        std::string indicator_left  = "Z";
        std::string indicator_right = "X";
        std::string headlight       = "L";
        std::string high_beam       = "K";
        std::string fog_light       = "F";
        std::string hazard          = "H";

        double steer_rate         = 2.0;  // /s, full lock in 0.5 s
        double centering_rate     = 3.0;  // /s, return to center
        double pedal_press_rate   = 4.0;  // /s, full press in 0.25 s
        double pedal_release_rate = 6.0;  // /s
    } keyboard;

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

    // FFB parameters (v5: physics-inspired model)
    struct
    {
        double sat_gain            = 0.08;  // reactive SAT strength (lat_accel -> force)
        double sat_centering_gain  = 1.50;  // caster trail centering (steering_angle -> force)
        double friction_base       = 0.12;  // static friction magnitude
        double friction_speed_gain = 0.04;  // additional friction proportional to speed
        double damper_base         = 0.02;  // low-speed damping coefficient
        double damper_speed_gain   = 0.06;  // additional damping proportional to speed
        double soft_stop_gain      = 0.5;   // resistance near steering lock
        double lock_angle          = 0.7;   // steering lock angle [rad]
        double assist_low_speed    = 0.90;  // power assist ratio at 0 m/s
        double assist_high_speed   = 0.20;  // power assist ratio at 30 m/s
        double max_force           = 1.0;   // output clamp [-1, 1]
        bool   disable_non_realtime = true;

        // feature:F7 (F7b) — FFB target-angle tracking (AD⇄手動 override).
        // When enabled, the FFB adds a PID servo term that drives the physical
        // wheel toward a target angle supplied by the AD stack (via
        // IFFBSink::SetSteerTarget). The commanded force + position error are
        // then read by OverrideManager as a torque-proxy intervention signal:
        // sustained push above the force/dev thresholds latches to MANUAL.
        // Default OFF so existing behavior (ManualDrive-only FFB) is unchanged.
        // Numbers from scripts/ffb_spike/README.md §1e/§2e (G29-calibrated,
        // NORMALIZED axis-fraction units — NOT radians).
        struct
        {
            bool   enabled                              = false;
            double kp                                   = 4.0;
            double kd                                   = 0.35;
            double max_force                            = 0.6;
            double hard_stop_zone                       = 0.85;
            double override_steer_force_threshold       = 0.20;
            double override_steer_dev_threshold         = 0.04;
            double override_sustain_time                = 0.10;  // seconds
        } target_track;
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
