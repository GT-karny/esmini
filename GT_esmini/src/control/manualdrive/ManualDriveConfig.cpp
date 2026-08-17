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

    // Record the directory this config file lives in so an input source
    // (ScriptedInputSource) can resolve a config-relative path itself,
    // without ControllerManualDrive having to thread the config path through
    // a second channel. Not a JSON key -- filepath is the argument this
    // function was called with, not something read from the file.
    {
        const size_t slash = filepath.find_last_of("/\\");
        config_dir = (slash == std::string::npos) ? std::string() : filepath.substr(0, slash);
    }

    // feature:F8 -- retired key. Polarity now lives in the calibrated pair on
    // every axis (steer_raw_center/steer_raw_full), so a leftover
    // "steer_invert": true would otherwise be ignored SILENTLY and the wheel
    // would steer the opposite way from the last run with no explanation.
    bool legacy_steer_invert = false;

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
        parse_int("auto_resume_button", sdl2.auto_resume_button);

        // feature:F8 -- wheel axis assignment + raw-range calibration.
        // Flat, globally-unique on-disk keys, same discipline as every other
        // block in this file (see the PARSER NOTE in ManualDriveConfig.hpp).
        //
        // NOT ALIASED, verified rather than assumed: the keyboard block below
        // parses "throttle"/"brake"/"clutch", and key_matches searches for the
        // key WITH its closing quote ("throttle"), so the line
        // `"throttle_axis": 1` cannot match it and vice versa.
        //
        // No bool among them: polarity lives in the calibrated pair on every
        // axis (WheelAxisMapping.hpp). parse_bool would also be unusable for
        // these keys -- it treats any value containing "1" as true, and every
        // *_axis key can legitimately be -1.
        parse_int ("steer_axis",             sdl2.axes.steer.index);
        parse_bool("steer_invert",           legacy_steer_invert);  // retired; warned about below
        parse_int ("steer_raw_center",       sdl2.axes.steer.raw_center);
        parse_int ("steer_raw_full",         sdl2.axes.steer.raw_full);
        parse_int ("throttle_axis",          sdl2.axes.throttle.index);
        parse_int ("throttle_raw_released",  sdl2.axes.throttle.raw_released);
        parse_int ("throttle_raw_full",      sdl2.axes.throttle.raw_full);
        parse_int ("brake_axis",             sdl2.axes.brake.index);
        parse_int ("brake_raw_released",     sdl2.axes.brake.raw_released);
        parse_int ("brake_raw_full",         sdl2.axes.brake.raw_full);
        parse_int ("clutch_axis",            sdl2.axes.clutch.index);
        parse_int ("clutch_raw_released",    sdl2.axes.clutch.raw_released);
        parse_int ("clutch_raw_full",        sdl2.axes.clutch.raw_full);

        // SDL2 keyboard input (key names are SDL scancode names)
        parse_string("steer_left",      keyboard.steer_left);
        parse_string("steer_right",     keyboard.steer_right);
        parse_string("throttle",        keyboard.throttle);
        parse_string("brake",           keyboard.brake);
        parse_string("clutch",          keyboard.clutch);
        parse_string("upshift",         keyboard.upshift);
        parse_string("downshift",       keyboard.downshift);
        parse_string("override_key",    keyboard.override_key);
        parse_string("indicator_left",  keyboard.indicator_left);
        parse_string("indicator_right", keyboard.indicator_right);
        parse_string("headlight",       keyboard.headlight);
        parse_string("high_beam",       keyboard.high_beam);
        parse_string("fog_light",       keyboard.fog_light);
        parse_string("hazard",          keyboard.hazard);
        parse_double("steer_rate",         keyboard.steer_rate);
        parse_double("centering_rate",     keyboard.centering_rate);
        parse_double("pedal_press_rate",   keyboard.pedal_press_rate);
        parse_double("pedal_release_rate", keyboard.pedal_release_rate);

        // Indicator auto-cancel
        parse_double("indicator_cancel_angle", indicator_cancel_angle);

        // Network input
        parse_string("transport_type", input_network.transport_type);
        parse_int("port", input_network.port);
        parse_string("level", input_network.level);

        // Input: scripted profile playback (req-vd-ad:REQ-AD-025..031,
        // vd-func:FUNC-075). On-disk key is "input_scripted_profile_file",
        // not the bare "profile_file" a nested-object reading of
        // ManualDriveConfig.hpp's input_scripted block might suggest -- see
        // the PARSER NOTE on the `adas` member below for why every key in
        // this file must be flat and globally unique regardless of JSON
        // nesting.
        parse_string("input_scripted_profile_file", input_scripted.profile_file);

        // ManualDrive ADAS -- phase A only. req-vd-ad:REQ-AD-025,
        // vd-func:FUNC-075. See ManualDriveConfig.hpp's `adas` member for the
        // PARSER NOTE explaining why every one of these on-disk keys is flat
        // and prefixed ("adas_aeb_enabled", not "enabled") rather than
        // matching the human-readable nested JSON shape.
        parse_bool("adas_aeb_enabled", adas.aeb.enabled);
        parse_bool("adas_aeb_kickdown_suppress_enabled", adas.aeb.kickdown_suppress_enabled);
        parse_double("adas_aeb_warning_ttc_threshold_s", adas.aeb.warning_ttc_threshold_s);
        // req-vd-ad:REQ-AD-028 (phase B) -- the second half of the FCW gate.
        // See ManualDriveConfig.hpp's field comment: exposing only the TTC
        // half made calibrating the warning point impossible whenever the
        // required-deceleration half was the binding one (design §9/§12's
        // recorded phase-A gap).
        parse_double("adas_aeb_warning_min_a_req_mps2", adas.aeb.warning_min_a_req_mps2);
        parse_double("adas_brake_full_decel_mps2", adas.brake_control.full_brake_decel_mps2);
        parse_double("adas_brake_kp", adas.brake_control.brake_kp);
        parse_double("adas_brake_ki", adas.brake_control.brake_ki);
        parse_double("adas_kickdown_threshold", adas.kickdown_threshold);
        parse_double("adas_kickdown_release_threshold", adas.kickdown_release_threshold);

        // ManualDrive ADAS phase C -- ACC (req-vd-ad:REQ-AD-026 / REQ-AD-031,
        // vd-func:FUNC-079) and MSL (req-vd-ad:REQ-AD-030, vd-func:FUNC-081).
        // Same flat-and-prefixed on-disk key discipline as the AEB block
        // above; see ManualDriveConfig.hpp's PARSER NOTE.
        parse_bool  ("adas_acc_enabled", adas.acc.enabled);
        parse_double("adas_acc_set_speed_step_mps", adas.acc.set_speed_step_mps);
        parse_double("adas_acc_thw_stage_short_s", adas.acc.thw_stage_short_s);
        parse_double("adas_acc_thw_stage_mid_s", adas.acc.thw_stage_mid_s);
        parse_double("adas_acc_thw_stage_long_s", adas.acc.thw_stage_long_s);
        parse_int   ("adas_acc_thw_default_stage", adas.acc.thw_default_stage);
        parse_double("adas_acc_min_speed_mps", adas.acc.min_speed_mps);
        parse_double("adas_acc_max_speed_mps", adas.acc.max_speed_mps);
        parse_bool  ("adas_acc_respect_speed_limit", adas.acc.respect_speed_limit);
        parse_double("adas_acc_accel_max_mps2", adas.acc.accel_max_mps2);
        parse_double("adas_acc_decel_max_mps2", adas.acc.decel_max_mps2);
        parse_double("adas_acc_full_brake_decel_mps2", adas.acc.full_brake_decel_mps2);
        parse_double("adas_acc_full_throttle_accel_mps2", adas.acc.full_throttle_accel_mps2);
        parse_double("adas_acc_speed_kp", adas.acc.speed_kp);
        parse_double("adas_acc_speed_ki", adas.acc.speed_ki);
        parse_double("adas_acc_speed_deadband_mps", adas.acc.speed_deadband_mps);
        parse_double("adas_acc_accel_override_threshold", adas.acc.accel_override_threshold);
        parse_double("adas_acc_brake_cancel_threshold", adas.acc.brake_cancel_threshold);
        parse_bool  ("adas_acc_stop_and_go_enabled", adas.acc.stop_and_go.enabled);
        parse_bool  ("adas_acc_stop_at_traffic_light", adas.acc.stop_and_go.stop_at_traffic_light);
        parse_bool  ("adas_acc_stop_at_stop_sign", adas.acc.stop_and_go.stop_at_stop_sign);
        parse_double("adas_acc_restart_accel_threshold", adas.acc.stop_and_go.restart_accel_threshold);
        parse_double("adas_acc_hold_brake", adas.acc.stop_and_go.hold_brake);
        parse_double("adas_acc_stop_speed_eps_mps", adas.acc.stop_and_go.stop_speed_eps_mps);

        parse_bool  ("adas_msl_enabled", adas.msl.enabled);
        parse_bool  ("adas_msl_speed_limit_linked", adas.msl.speed_limit_linked);
        parse_double("adas_msl_taper_band_mps", adas.msl.taper_band_mps);

        // req-vd-ad:REQ-AD-027 (phase D). `adas_lka_warning_only` is the LDW
        // mode switch, not a second function -- see the header's own note.
        parse_bool  ("adas_lka_enabled", adas.lka.enabled);
        parse_bool  ("adas_lka_warning_only", adas.lka.warning_only);
        parse_double("adas_lka_min_speed_mps", adas.lka.min_speed_mps);
        parse_double("adas_lka_max_speed_mps", adas.lka.max_speed_mps);
        parse_double("adas_lka_tlc_threshold_s", adas.lka.tlc_threshold_s);
        parse_double("adas_lka_margin_threshold_m", adas.lka.margin_threshold_m);
        parse_double("adas_lka_release_margin_m", adas.lka.release_margin_m);
        parse_double("adas_lka_kp_offset", adas.lka.kp_offset);
        parse_double("adas_lka_kd_lateral", adas.lka.kd_lateral);
        parse_double("adas_lka_correction_max", adas.lka.correction_max);
        parse_double("adas_lka_correction_rate_max", adas.lka.correction_rate_max);
        parse_double("adas_lka_steer_override_rate", adas.lka.steer_override_rate);
        parse_double("adas_lka_steer_override_hold_s", adas.lka.steer_override_hold_s);

        // ADAS operating controls (design §9's `buttons` block).
        parse_int("acc_toggle_button", adas_buttons.acc_toggle_button);
        parse_int("acc_set_resume_button", adas_buttons.acc_set_resume_button);
        parse_int("acc_speed_up_button", adas_buttons.acc_speed_up_button);
        parse_int("acc_speed_down_button", adas_buttons.acc_speed_down_button);
        parse_int("acc_thw_cycle_button", adas_buttons.acc_thw_cycle_button);
        parse_int("msl_toggle_button", adas_buttons.msl_toggle_button);

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

        // FFB target-tracking (F7b). Flat unique keys under ffb; the JSON
        // groups them into an "ffb.target_track" object purely for humans.
        parse_bool  ("target_track_enabled",                         ffb.target_track.enabled);
        parse_double("target_track_kp",                              ffb.target_track.kp);
        parse_double("target_track_kd",                              ffb.target_track.kd);
        parse_double("target_track_max_force",                       ffb.target_track.max_force);
        parse_double("target_track_hard_stop_zone",                  ffb.target_track.hard_stop_zone);
        parse_double("target_track_friction_ff",                     ffb.target_track.friction_ff);
        parse_double("target_track_friction_ff_eps",                 ffb.target_track.friction_ff_eps);
        parse_double("target_track_feel_ratio",                      ffb.target_track.feel_ratio);
        parse_double("target_track_override_steer_force_threshold",  ffb.target_track.override_steer_force_threshold);
        parse_double("target_track_override_steer_dev_threshold",    ffb.target_track.override_steer_dev_threshold);
        parse_double("target_track_override_sustain_time",           ffb.target_track.override_sustain_time);
        parse_double("target_track_override_target_rate_gate",         ffb.target_track.override_target_rate_gate);
        parse_double("target_track_override_position_error_rate_gate", ffb.target_track.override_position_error_rate_gate);
        parse_double("target_track_override_residual_threshold",       ffb.target_track.override_residual_threshold);
        parse_double("target_track_override_residual_reanchor_tau",    ffb.target_track.override_residual_reanchor_tau);
        parse_double("target_track_override_shadow_breakaway",         ffb.target_track.override_shadow_breakaway);
        parse_double("target_track_override_shadow_breakaway_left",     ffb.target_track.override_shadow_breakaway_left);
        parse_double("target_track_override_shadow_breakaway_right",    ffb.target_track.override_shadow_breakaway_right);
        parse_double("target_track_override_shadow_motion_epsilon",    ffb.target_track.override_shadow_motion_epsilon);
        parse_double("target_track_override_shadow_kinetic",           ffb.target_track.override_shadow_kinetic);
        parse_double("target_track_override_shadow_force_to_velocity", ffb.target_track.override_shadow_force_to_velocity);
        parse_double("target_track_override_shadow_v_max",             ffb.target_track.override_shadow_v_max);
        parse_double("target_track_override_shadow_velocity_tau",      ffb.target_track.override_shadow_velocity_tau);
        parse_double("target_track_override_shadow_dead_time",         ffb.target_track.override_shadow_dead_time);
        parse_double("target_track_override_shadow_onset_grace",       ffb.target_track.override_shadow_onset_grace);
        parse_double("target_track_override_shadow_motion_rate_eps",   ffb.target_track.override_shadow_motion_rate_eps);
        // feature:F7 unattended-run safety watchdog (0 = disabled).
        parse_double("safety_max_saturation_seconds", ffb.safety.max_saturation_seconds);
        parse_double("safety_max_runtime_seconds",    ffb.safety.max_runtime_seconds);
        parse_double("safety_saturation_ratio",       ffb.safety.saturation_ratio);

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
        parse_bool("button_takeover", override_cfg.button_takeover);
    }

    LOG_INFO("ManualDriveConfig: Parsed FFB — sat_gain={:.3f} centering={:.3f} fric_base={:.3f} "
             "assist_lo={:.2f} assist_hi={:.2f} max_force={:.2f}",
             ffb.sat_gain, ffb.sat_centering_gain, ffb.friction_base,
             ffb.assist_low_speed, ffb.assist_high_speed, ffb.max_force);
    LOG_INFO("ManualDriveConfig: FFB target_track enabled={} kp={:.2f} kd={:.2f} max_force={:.2f} "
             "force_thr={:.3f} dev_thr={:.3f} sustain={:.3f}s",
             ffb.target_track.enabled, ffb.target_track.kp, ffb.target_track.kd,
             ffb.target_track.max_force, ffb.target_track.override_steer_force_threshold,
             ffb.target_track.override_steer_dev_threshold, ffb.target_track.override_sustain_time);
    if (legacy_steer_invert)
    {
        LOG_WARN("ManualDriveConfig: 'steer_invert' is no longer used and was IGNORED. Steering "
                 "polarity now comes from steer_raw_center/steer_raw_full (raw_full is full RIGHT) "
                 "-- mirror them instead: steer_raw_full = 2*steer_raw_center - steer_raw_full, or "
                 "press Flip in the GUI's Axis Mapping panel.");
    }
    // feature:F8 -- logged unconditionally: when a wheel misbehaves, "which
    // axis was this build actually reading?" is the first question, and the
    // answer must be in the log of the run that misbehaved.
    LOG_INFO("ManualDriveConfig: axes steer=a{}{} (center={} full={}) throttle=a{} ({}..{}) "
             "brake=a{} ({}..{}) clutch=a{} ({}..{})",
             sdl2.axes.steer.index, sdl2.axes.steer.SignFactor() < 0.0 ? " (counts up to the left)" : "",
             sdl2.axes.steer.raw_center, sdl2.axes.steer.raw_full,
             sdl2.axes.throttle.index, sdl2.axes.throttle.raw_released, sdl2.axes.throttle.raw_full,
             sdl2.axes.brake.index, sdl2.axes.brake.raw_released, sdl2.axes.brake.raw_full,
             sdl2.axes.clutch.index, sdl2.axes.clutch.raw_released, sdl2.axes.clutch.raw_full);

    return true;
}

} // namespace gt_esmini
