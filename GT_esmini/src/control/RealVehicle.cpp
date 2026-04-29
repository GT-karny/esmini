#include "gt_esmini/control/RealVehicle.hpp"
#include "logger.hpp"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Clamp helper
template <typename T>
T Clamp(T val, T min, T max) {
    if (val < min) return min;
    if (val > max) return max;
    return val;
}

namespace gt_esmini
{

namespace
{

// Minimal JSON helpers (line/text-based) for additional sections we need to
// parse out of real_vehicle_params.json without pulling in a JSON library.

std::string ExtractBlock(const std::string& text, size_t start)
{
    if (start >= text.size() || text[start] != '{')
        return "";
    int depth = 0;
    size_t end = start;
    for (; end < text.size(); ++end)
    {
        if (text[end] == '{') depth++;
        else if (text[end] == '}') { depth--; if (depth == 0) break; }
    }
    return text.substr(start, end - start + 1);
}

std::string FindSection(const std::string& content, const std::string& key)
{
    size_t pos = content.find("\"" + key + "\"");
    if (pos == std::string::npos) return "";
    size_t lb = content.find('{', pos);
    if (lb == std::string::npos) return "";
    return ExtractBlock(content, lb);
}

double ParseDoubleIn(const std::string& block, const std::string& key, double fallback)
{
    size_t pos = block.find("\"" + key + "\"");
    if (pos == std::string::npos) return fallback;
    size_t colon = block.find(':', pos);
    if (colon == std::string::npos) return fallback;
    try { return std::stod(block.substr(colon + 1)); }
    catch (...) { return fallback; }
}

std::vector<double> ParseDoubleArrayIn(const std::string& block, const std::string& key,
                                        const std::vector<double>& fallback)
{
    size_t pos = block.find("\"" + key + "\"");
    if (pos == std::string::npos) return fallback;
    size_t lb = block.find('[', pos);
    size_t rb = block.find(']', lb);
    if (lb == std::string::npos || rb == std::string::npos) return fallback;
    std::string inner = block.substr(lb + 1, rb - lb - 1);
    std::vector<double> out;
    std::stringstream ss(inner);
    std::string tok;
    while (std::getline(ss, tok, ','))
    {
        try { out.push_back(std::stod(tok)); }
        catch (...) {}
    }
    return out.empty() ? fallback : out;
}

double SmoothStep01(double x)
{
    x = std::clamp(x, 0.0, 1.0);
    return x * x * (3.0 - 2.0 * x);
}

} // namespace

RealVehicle::RealVehicle() : vehicle::Vehicle()
{
    // Default tuning values (Civic FL1 1.5L turbo class)
    idle_rpm_ = 700.0;
    max_rpm_ = 6500.0;
    rpm_ = idle_rpm_;
    gear_ratio_ = 3.5; // Legacy single-ratio fallback

    // Physics State
    roll_ = 0.0;
    pitch_rate_ = 0.0;
    roll_rate_ = 0.0;

    ConfigureATAndEngine();
}

void RealVehicle::ConfigureATAndEngine()
{
    // Configure AutoTransmission schedule from current params
    AutoTransmission::Params atp;
    atp.schedule.shift_up_kmh   = {15, 30, 50, 75, 100};
    atp.schedule.shift_down_kmh = {10, 22, 40, 60,  85};
    atp.schedule.kickdown_gain  = 0.35;
    atp.schedule.brake_downshift_threshold = 0.4;
    atp.schedule.min_gear_hold_s = 0.5;
    atp.schedule.max_gear        = static_cast<int>(params_.gear_ratios.size());
    atp.manual_override_timeout_s = 10.0;
    atp.paddle_simul_press_threshold_s = 0.15;
    atp.low_speed_kmh_for_revert  = 5.0;
    auto_trans_.SetParams(atp);
    auto_trans_.Reset();

    // Configure EngineModel for 1.5L Turbo (defaults match Civic FL1 class)
    EngineModel::Params ep;
    ep.idle_rpm             = idle_rpm_;
    ep.max_rpm              = max_rpm_;
    ep.rev_limit_rpm        = max_rpm_;
    ep.torque_peak_nm       = 260.0;
    ep.torque_flat_low_rpm  = 1600.0;
    ep.torque_flat_high_rpm = 5000.0;
    ep.torque_redline_factor = 0.65;
    ep.engine_inertia_tau_s = 0.15;
    ep.idle_governor_gain   = 0.5;
    engine_.SetParams(ep);
    engine_.Reset();
    rpm_ = idle_rpm_;
    at_seeded_ = false;
    at_manual_mode_ = false;
    engine_torque_nm_ = 0.0;
    gear_ = 1;
}

void RealVehicle::LoadParameters(const std::string& filename)
{
    std::ifstream file(filename);
    if (!file.is_open())
    {
        LOG_INFO("RealVehicle: params NOT FOUND, using defaults: {}", filename);
        return;
    }
    LOG_INFO("RealVehicle: Loading params from: {}", filename);

    // Read file once for both line-based and section-based parsing.
    std::string full((std::istreambuf_iterator<char>(file)),
                      std::istreambuf_iterator<char>());
    file.close();

    // -- Line-based scalar parsing (top-level only) for backward compat --
    {
        std::stringstream stream(full);
        std::string line;
        int brace_depth = 0;
        while (std::getline(stream, line))
        {
            for (char ch : line)
            {
                if (ch == '{') ++brace_depth;
                else if (ch == '}') --brace_depth;
            }
            if (brace_depth > 1) continue;

            auto parse_val = [&](const std::string& key, double& val)
            {
                if (line.find(key) != std::string::npos) {
                    size_t colon = line.find(":");
                    if (colon != std::string::npos) {
                        try { val = std::stod(line.substr(colon + 1)); }
                        catch (...) {}
                    }
                }
            };

            parse_val("pitch_stiffness", params_.pitch_stiffness);
            parse_val("pitch_damping", params_.pitch_damping);
            parse_val("roll_stiffness", params_.roll_stiffness);
            parse_val("roll_damping", params_.roll_damping);
            parse_val("mass_height", params_.mass_height);
            parse_val("center_of_rotation_z_offset", params_.center_of_rotation_z_offset);
            parse_val("max_pitch_deg", params_.max_pitch_deg);
            parse_val("max_roll_deg", params_.max_roll_deg);
            parse_val("steer_gain", params_.steer_gain);
            parse_val("max_speed", params_.max_speed);
            parse_val("max_acc", params_.max_acc);
            parse_val("max_dec", params_.max_dec);
            parse_val("idle_rpm", idle_rpm_);
            parse_val("max_rpm", max_rpm_);
            parse_val("gear_ratio", gear_ratio_);
            parse_val("reverse_gear_ratio", params_.reverse_gear_ratio);

            parse_val("drag_coeff", params_.drag_coeff);
            parse_val("engine_brake", params_.engine_brake);
            parse_val("torque_peak_pos", params_.torque_peak_pos);
            parse_val("torque_min", params_.torque_min);

            parse_val("understeer_factor", params_.understeer_factor);
            parse_val("critical_speed", params_.critical_speed);
            parse_val("max_understeer_reduction", params_.max_understeer_reduction);

            // AT physical resistance / aero
            parse_val("mass_kg", params_.mass_kg);
            parse_val("aero_drag_cd", params_.aero_drag_cd);
            parse_val("frontal_area_m2", params_.frontal_area_m2);
            parse_val("air_density", params_.air_density);
            parse_val("rolling_resistance_coeff", params_.rolling_resistance_coeff);
        }
    }

    // -- Forward-AT driveline / engine params (sectioned JSON) --
    std::string trans_blk = FindSection(full, "transmission");
    if (!trans_blk.empty())
    {
        params_.gear_ratios          = ParseDoubleArrayIn(trans_blk, "gear_ratios", params_.gear_ratios);
        params_.final_drive_ratio    = ParseDoubleIn(trans_blk, "final_drive_ratio", params_.final_drive_ratio);
        params_.reverse_ratio        = ParseDoubleIn(trans_blk, "reverse_ratio", params_.reverse_ratio);
        params_.drivetrain_efficiency = ParseDoubleIn(trans_blk, "drivetrain_efficiency", params_.drivetrain_efficiency);
        std::string tc_blk = FindSection(trans_blk, "torque_converter");
        if (!tc_blk.empty())
        {
            params_.v_lockup_mps    = ParseDoubleIn(tc_blk, "v_lockup_mps",    params_.v_lockup_mps);
            params_.tc_slip_rpm_max = ParseDoubleIn(tc_blk, "slip_rpm_max",    params_.tc_slip_rpm_max);
        }
        params_.engine_drag_base_nm  = ParseDoubleIn(trans_blk, "engine_drag_base_nm",  params_.engine_drag_base_nm);
        params_.engine_drag_per_krpm = ParseDoubleIn(trans_blk, "engine_drag_per_krpm", params_.engine_drag_per_krpm);
    }
    // Fallback: pull gear_ratios / final_drive_ratio from shift_schedule if no
    // dedicated transmission section was provided (preserves older configs).
    {
        std::string shf = FindSection(full, "shift_schedule");
        if (!shf.empty())
        {
            params_.gear_ratios       = ParseDoubleArrayIn(shf, "gear_ratios", params_.gear_ratios);
            params_.final_drive_ratio = ParseDoubleIn(shf, "final_drive_ratio", params_.final_drive_ratio);
        }
    }

    // -- Engine model params --
    EngineModel::Params ep = engine_.GetParams();
    std::string eng_blk = FindSection(full, "engine");
    if (!eng_blk.empty())
    {
        ep.idle_rpm             = ParseDoubleIn(eng_blk, "idle_rpm",            ep.idle_rpm);
        ep.max_rpm              = ParseDoubleIn(eng_blk, "max_rpm",             ep.max_rpm);
        ep.rev_limit_rpm        = ParseDoubleIn(eng_blk, "rev_limit_rpm",       ep.max_rpm);
        ep.torque_peak_nm       = ParseDoubleIn(eng_blk, "torque_peak_nm",      ep.torque_peak_nm);
        ep.torque_flat_low_rpm  = ParseDoubleIn(eng_blk, "torque_flat_rpm_low", ep.torque_flat_low_rpm);
        ep.torque_flat_high_rpm = ParseDoubleIn(eng_blk, "torque_flat_rpm_high",ep.torque_flat_high_rpm);
        ep.torque_redline_factor = ParseDoubleIn(eng_blk, "torque_redline_factor", ep.torque_redline_factor);
        ep.engine_inertia_tau_s = ParseDoubleIn(eng_blk, "engine_inertia_tau_s", ep.engine_inertia_tau_s);
        ep.idle_governor_gain   = ParseDoubleIn(eng_blk, "idle_governor_gain",  ep.idle_governor_gain);
        ep.idle_creep_torque_nm = ParseDoubleIn(eng_blk, "idle_creep_torque_nm", ep.idle_creep_torque_nm);
        ep.idle_jitter.sigma_rpm = ParseDoubleIn(eng_blk, "idle_jitter_sigma_rpm", ep.idle_jitter.sigma_rpm);
        ep.idle_jitter.tau_s     = ParseDoubleIn(eng_blk, "idle_jitter_tau_s",     ep.idle_jitter.tau_s);
        ep.idle_jitter.seed      = static_cast<uint32_t>(
            ParseDoubleIn(eng_blk, "idle_jitter_seed", static_cast<double>(ep.idle_jitter.seed)));
    }
    // Keep idle/max in sync with the engine block when provided.
    idle_rpm_ = ep.idle_rpm;
    max_rpm_  = ep.max_rpm;
    engine_.SetParams(ep);

    // -- AT controller tuning --
    AutoTransmission::Params atp = auto_trans_.GetParams();
    atp.schedule.max_gear = static_cast<int>(params_.gear_ratios.size());
    std::string atc_blk = FindSection(full, "at_controller");
    if (!atc_blk.empty())
    {
        atp.manual_override_timeout_s = ParseDoubleIn(atc_blk, "manual_override_timeout_s", atp.manual_override_timeout_s);
        atp.paddle_simul_press_threshold_s = ParseDoubleIn(atc_blk, "paddle_simul_press_threshold_s", atp.paddle_simul_press_threshold_s);
        atp.schedule.min_gear_hold_s = ParseDoubleIn(atc_blk, "min_gear_hold_s", atp.schedule.min_gear_hold_s);
        atp.schedule.kickdown_gain   = ParseDoubleIn(atc_blk, "kickdown_factor", 1.0 + atp.schedule.kickdown_gain) - 1.0;
        atp.low_speed_kmh_for_revert = ParseDoubleIn(atc_blk, "low_speed_kmh_for_revert", atp.low_speed_kmh_for_revert);
    }
    // Pick up the comfort-mode schedule (HVDEstimator's existing data) if
    // present, so AT and HVDEstimator stay in sync by default.
    {
        std::string shf = FindSection(full, "shift_schedule");
        if (!shf.empty())
        {
            std::string modes = FindSection(shf, "modes");
            std::string cf    = modes.empty() ? std::string() : FindSection(modes, "comfort");
            if (!cf.empty())
            {
                atp.schedule.shift_up_kmh = ParseDoubleArrayIn(cf, "shift_up_kmh", atp.schedule.shift_up_kmh);
                atp.schedule.shift_down_kmh = ParseDoubleArrayIn(cf, "shift_down_kmh", atp.schedule.shift_down_kmh);
                atp.schedule.kickdown_gain = ParseDoubleIn(cf, "kickdown_gain", atp.schedule.kickdown_gain);
                atp.schedule.brake_downshift_threshold = ParseDoubleIn(cf, "brake_downshift_threshold", atp.schedule.brake_downshift_threshold);
                atp.schedule.min_gear_hold_s = ParseDoubleIn(cf, "min_gear_hold_s", atp.schedule.min_gear_hold_s);
                // Shift-event parameters (shared with HVDEstimator's model).
                params_.shift_event_duration_s = ParseDoubleIn(cf, "shift_event_duration_s", params_.shift_event_duration_s);
                params_.upshift_dip_rpm        = ParseDoubleIn(cf, "upshift_dip_rpm",        params_.upshift_dip_rpm);
                params_.downshift_blip_rpm     = ParseDoubleIn(cf, "downshift_blip_rpm",     params_.downshift_blip_rpm);
                params_.shift_torque_factor    = ParseDoubleIn(cf, "shift_torque_factor",    params_.shift_torque_factor);
            }
        }
    }
    auto_trans_.SetParams(atp);

    SetMaxAcc(params_.max_acc);
    SetMaxDec(params_.max_dec);
    SetMaxSpeed(params_.max_speed);

    LOG_INFO("RealVehicle: Loaded params: pitch_stiff={}, pitch_damp={}, roll_stiff={}, roll_damp={}, "
             "mass_h={}, max_pitch={}deg, max_roll={}deg, steer_gain={}, max_acc={}, max_dec={}, max_spd={}, "
             "drag={}, eng_brake={}, torque_peak={}, torque_min={}",
             params_.pitch_stiffness, params_.pitch_damping,
             params_.roll_stiffness, params_.roll_damping,
             params_.mass_height, params_.max_pitch_deg, params_.max_roll_deg,
             params_.steer_gain, params_.max_acc, params_.max_dec, params_.max_speed,
             params_.drag_coeff, params_.engine_brake, params_.torque_peak_pos, params_.torque_min);
    LOG_INFO("RealVehicle: AT/Engine: gears={}, final={}, mass={}kg, peak_torque={}Nm @ {}-{}rpm, idle={}, max={}",
             params_.gear_ratios.size(), params_.final_drive_ratio, params_.mass_kg,
             ep.torque_peak_nm, ep.torque_flat_low_rpm, ep.torque_flat_high_rpm,
             ep.idle_rpm, ep.max_rpm);
}

void RealVehicle::GetBodyPositionOffset(double& dx, double& dy, double& dz)
{
    double z = params_.center_of_rotation_z_offset;
    if (z == 0.0) z = 0.4;

    dx = 0.0;
    dy = 0.0;
    dz = z * std::cos(pitch_) * std::cos(roll_) - z;
}

double RealVehicle::GetTorque(double current_rpm) const
{
    // Legacy normalized torque curve [0..1]. Used by UpdatePhysics() only.
    double normalized_rpm = (current_rpm - idle_rpm_) / (max_rpm_ - idle_rpm_);
    normalized_rpm = std::max(0.0, std::min(1.0, normalized_rpm));

    double p = params_.torque_peak_pos;
    double half_width = std::max(p, 1.0 - p);
    double shape = 1.0 - ((normalized_rpm - p) / half_width) * ((normalized_rpm - p) / half_width);
    shape = std::max(0.0, shape);

    double torque = params_.torque_min + (1.0 - params_.torque_min) * shape;
    return torque;
}

double RealVehicle::GetTorqueOutput() const
{
    // Prefer the EngineModel torque (Nm) normalized into [0..1] of peak when
    // the AT path has been driven; otherwise fall back to legacy curve.
    if (engine_torque_nm_ > 0.0 || at_seeded_)
    {
        double peak = std::max(1.0, engine_.GetParams().torque_peak_nm);
        return std::clamp(engine_torque_nm_ / peak, 0.0, 1.0);
    }
    return GetTorque(rpm_);
}

void RealVehicle::SetTerrainAttitude(double pitch, double roll)
{
    terrain_pitch_ = pitch;
    terrain_roll_ = roll;
}

void RealVehicle::GetCombinedAttitude(double& pitch, double& roll) const
{
    pitch = terrain_pitch_ + dynamic_pitch_;
    roll = terrain_roll_ + dynamic_roll_;
}

void RealVehicle::StepLateralAndAttitude(double dt, double steering, double long_acc)
{
    // 3. Steering
    double steer_max = params_.steer_gain;
    double target_wheel_angle = -steering * steer_max;

    if (params_.understeer_factor > 0.0) {
        double speed_abs = std::abs(speed_);
        if (speed_abs > params_.critical_speed) {
            double speed_ratio = speed_abs / params_.critical_speed;
            double understeer_coeff = params_.understeer_factor * (speed_ratio * speed_ratio - 1.0);
            understeer_coeff = std::min(understeer_coeff, params_.max_understeer_reduction);
            double grip_factor = 1.0 / (1.0 + understeer_coeff);
            target_wheel_angle *= grip_factor;
        }
    }

    double steer_rate = 5.0;
    double diff = target_wheel_angle - wheelAngle_;
    if (std::abs(diff) < steer_rate * dt) {
        wheelAngle_ = target_wheel_angle;
    } else {
        wheelAngle_ += (diff > 0 ? 1 : -1) * steer_rate * dt;
    }

    // 4. Update Position (Kinematic Bicycle)
    vehicle::Vehicle::Update(dt);
    if (heading_ > M_PI) heading_ -= 2.0 * M_PI;

    // 5. Pitch and Roll (spring-damper)
    double yaw_rate = headingDot_;
    double lat_acc = speed_ * yaw_rate;

    latAcc_  = lat_acc;
    longAcc_ = long_acc;

    double pitch_forcing = -params_.mass_height * long_acc;
    double pitch_acc = (-params_.pitch_stiffness * dynamic_pitch_) - (params_.pitch_damping * pitch_rate_) + pitch_forcing;
    pitch_rate_ += pitch_acc * dt;
    dynamic_pitch_ += pitch_rate_ * dt;

    double roll_forcing = params_.mass_height * lat_acc;
    double roll_acc = (-params_.roll_stiffness * dynamic_roll_) - (params_.roll_damping * roll_rate_) + roll_forcing;
    roll_rate_ += roll_acc * dt;
    dynamic_roll_ += roll_rate_ * dt;

    double lim_p = params_.max_pitch_deg * M_PI / 180.0;
    double lim_r = params_.max_roll_deg * M_PI / 180.0;
    if (dynamic_pitch_ > lim_p)       { dynamic_pitch_ = lim_p;  pitch_rate_ = std::min(pitch_rate_, 0.0); }
    else if (dynamic_pitch_ < -lim_p) { dynamic_pitch_ = -lim_p; pitch_rate_ = std::max(pitch_rate_, 0.0); }
    if (dynamic_roll_ > lim_r)       { dynamic_roll_ = lim_r;  roll_rate_ = std::min(roll_rate_, 0.0); }
    else if (dynamic_roll_ < -lim_r) { dynamic_roll_ = -lim_r; roll_rate_ = std::max(roll_rate_, 0.0); }

    pitch_ = terrain_pitch_ + dynamic_pitch_;
    roll_  = terrain_roll_ + dynamic_roll_;
}

void RealVehicle::UpdatePhysics(double dt, double throttle, double brake, double steering, int gear)
{
    if (dt <= 0.00001) return;

    gear_ = gear;

    // Legacy single-gear engine + RPM heuristic
    double target_rpm = idle_rpm_ + (max_rpm_ - idle_rpm_) * throttle;
    double rpm_change_rate = 2000.0;
    if (target_rpm > rpm_) {
        rpm_ += rpm_change_rate * dt;
        if (rpm_ > target_rpm) rpm_ = target_rpm;
    } else {
        rpm_ -= rpm_change_rate * dt;
        if (rpm_ < target_rpm) rpm_ = target_rpm;
    }
    double mechanical_min_rpm = std::abs(speed_) * 60.0 * gear_ratio_ * 2.0;
    if (rpm_ < mechanical_min_rpm) rpm_ = mechanical_min_rpm;
    rpm_ = Clamp(rpm_, idle_rpm_, max_rpm_);

    double available_torque = GetTorque(rpm_);
    double engine_force = available_torque * throttle * GetMaxAcc();
    if (gear_ == 0) engine_force = 0.0;
    else if (gear_ == -1) engine_force = -engine_force * params_.reverse_gear_ratio;

    double deceleration_force = brake * GetMaxDec();

    double drag_force = speed_ * speed_ * params_.drag_coeff;
    if (speed_ < 0) drag_force = -drag_force;
    if (throttle < 0.05)
    {
        if (speed_ > 0) drag_force += params_.engine_brake;
        else if (speed_ < 0) drag_force -= params_.engine_brake;
    }

    double acc = engine_force;
    if (speed_ > 0.01)        acc -= deceleration_force + drag_force;
    else if (speed_ < -0.01)  acc += deceleration_force + std::abs(drag_force);
    else if (brake > 0)       { acc = 0; speed_ = 0; }

    speed_ += acc * dt;
    if (speed_ < -20.0) speed_ = -20.0;

    // Reset AT-path bookkeeping so legacy callers see clean state if mixed.
    engine_torque_nm_ = 0.0;
    at_manual_mode_ = false;

    StepLateralAndAttitude(dt, steering, acc);
}

void RealVehicle::UpdatePhysicsAT(double dt, double throttle, double brake, double steering,
                                   bool paddle_up_pressed, bool paddle_down_pressed)
{
    if (dt <= 0.00001) return;

    // Seed transmission from initial speed once
    if (!at_seeded_)
    {
        auto_trans_.SeedFromSpeed(std::fabs(speed_) * 3.6);
        at_seeded_ = true;
    }

    // 1. Auto-transmission: paddles + lever -> gear command
    AutoTransmission::Inputs ati;
    ati.paddle_up_pressed   = paddle_up_pressed;
    ati.paddle_down_pressed = paddle_down_pressed;
    ati.throttle            = throttle;
    ati.brake               = brake;
    ati.speed_mps           = speed_;
    auto at_out = auto_trans_.Step(ati, dt);

    gear_           = at_out.gear_for_drivetrain;
    at_manual_mode_ = at_out.manual_mode;

    // 2. Driveline kinematics: wheel speed -> engine RPM target
    double abs_speed = std::fabs(speed_);
    double wheel_rps = abs_speed / (2.0 * M_PI * std::max(0.05, params_.wheel_radius_m));

    double total_ratio = 0.0;
    if (at_out.range == AutoTransmission::Range::DRIVE)
    {
        int gi = std::clamp(at_out.forward_gear, 1, static_cast<int>(params_.gear_ratios.size()));
        total_ratio = params_.gear_ratios[gi - 1] * params_.final_drive_ratio;
    }
    else if (at_out.range == AutoTransmission::Range::REVERSE)
    {
        total_ratio = params_.reverse_ratio * params_.final_drive_ratio;
    }
    // Neutral: total_ratio stays 0 (engine free-revs)

    // 3. Torque converter behaviour
    //
    // Real automotive torque converters: when slip is high (vehicle stopped,
    // engine wants to rev), the engine RPM is held by turbine load to a
    // throttle-dependent "stall speed" (typically ~2200 RPM at WOT for a
    // production passenger car), and the converter MULTIPLIES torque by up
    // to ~2x at full slip. As the impeller and turbine speeds equalise the
    // multiplication fades to 1.0 and the converter locks up.
    //
    // We model this with two coupled effects:
    //   - Engine target RPM = max(stall_target, geared_rpm)
    //   - Driveline torque  = engine_torque * tc_multiplier  (1..stall_mult)
    double v_lockup    = std::max(0.1, params_.v_lockup_mps);
    double slip_factor = SmoothStep01(abs_speed / v_lockup);          // 0=full slip, 1=locked
    bool   clutch_locked = (at_out.range != AutoTransmission::Range::NEUTRAL);

    double geared_rpm = wheel_rps * 60.0 * total_ratio;

    constexpr double kStallMultiplier = 2.0;     // torque ratio at full slip (typical TC)

    double target_rpm;
    if (at_out.range == AutoTransmission::Range::NEUTRAL)
    {
        // True neutral: engine free-revs against its own friction only.
        target_rpm = idle_rpm_ + (max_rpm_ - idle_rpm_) * std::clamp(throttle, 0.0, 1.0);
        clutch_locked = false;
    }
    else
    {
        // TC slip-RPM model: engine RPM = geared (turbine) RPM + slip,
        // where slip rises with throttle and decays toward lockup speed.
        // This replaces the previous max(stall, geared) clamp which froze
        // engine RPM at ~stall during low-speed acceleration. The flash-stall
        // peak (~idle + slip_rpm_max ≈ 2200) is preserved at WOT-from-rest;
        // beyond that, RPM climbs continuously with vehicle speed.
        double slip_rpm = std::clamp(throttle, 0.0, 1.0)
                        * params_.tc_slip_rpm_max
                        * (1.0 - slip_factor);
        target_rpm = std::max(idle_rpm_ * 1.05, geared_rpm + slip_rpm);
    }

    // Shift event: trigger torque cut + RPM dip/blip on gear change. Same
    // model as HVDEstimator (see HVDEstimator.cpp::EstimateRPM) so the
    // estimator and this real-physics path stay in sync.
    if (at_out.shifted_up)
    {
        shift_event_timer_s_ = params_.shift_event_duration_s;
        shift_event_dir_     = +1;
    }
    else if (at_out.shifted_down)
    {
        shift_event_timer_s_ = params_.shift_event_duration_s;
        shift_event_dir_     = -1;
    }

    // Apply RPM dip (upshift) / blip (downshift) for the duration of the event.
    if (shift_event_timer_s_ > 0.0 && params_.shift_event_duration_s > 0.0
        && at_out.range == AutoTransmission::Range::DRIVE)
    {
        double phase = std::clamp(shift_event_timer_s_ / params_.shift_event_duration_s, 0.0, 1.0);
        if (shift_event_dir_ > 0)
        {
            target_rpm = std::max(idle_rpm_ * 1.05, target_rpm - params_.upshift_dip_rpm * phase);
        }
        else if (shift_event_dir_ < 0)
        {
            target_rpm += params_.downshift_blip_rpm * phase;
        }
    }

    // 4. Engine: throttle + target RPM -> torque & rpm
    EngineModel::VehicleContext engine_vctx{abs_speed, slip_factor};
    engine_.Step(throttle, target_rpm, clutch_locked, engine_vctx, dt);
    rpm_ = engine_.GetRPM();
    engine_torque_nm_ = engine_.GetTorqueNm();

    // Torque cut during shift event: hydraulic overlap / clutch slip means
    // very little drive torque reaches the wheels for ~150-300ms. Modeled by
    // scaling the engine output torque while the timer is active.
    if (shift_event_timer_s_ > 0.0)
    {
        engine_torque_nm_ *= std::clamp(params_.shift_torque_factor, 0.0, 1.0);
        shift_event_timer_s_ = std::max(0.0, shift_event_timer_s_ - dt);
        if (shift_event_timer_s_ <= 0.0) shift_event_dir_ = 0;
    }

    // 5. Driveline force with torque-converter multiplication
    double mass = std::max(100.0, params_.mass_kg);
    double r    = std::max(0.05, params_.wheel_radius_m);
    double eta  = std::clamp(params_.drivetrain_efficiency, 0.1, 1.0);

    double tc_mult = 1.0;
    if (at_out.range != AutoTransmission::Range::NEUTRAL)
    {
        tc_mult = 1.0 + (1.0 - slip_factor) * (kStallMultiplier - 1.0);
    }

    double engine_force_n = 0.0;  // N at wheels
    if (at_out.range == AutoTransmission::Range::DRIVE)
    {
        engine_force_n =  engine_torque_nm_ * total_ratio * eta * tc_mult / r;
    }
    else if (at_out.range == AutoTransmission::Range::REVERSE)
    {
        engine_force_n = -engine_torque_nm_ * total_ratio * eta * tc_mult / r;
    }
    double engine_acc = engine_force_n / mass;
    // Grip cap: keep peak acceleration plausible (avoids first-gear "wheelspin to the moon").
    engine_acc = std::clamp(engine_acc, -GetMaxDec(), GetMaxAcc());

    // Brake force (deceleration, opposing motion)
    double brake_acc = brake * GetMaxDec();

    // Physical aerodynamic drag: F_drag = 0.5 * ρ * Cd * A * v²  (in motion direction)
    // The legacy drag_coeff value (~0.0013) was tuned for a constant-max-acc
    // engine model; it's ~5× too large when used with a real torque curve.
    double F_drag_n = 0.5 * params_.air_density * params_.aero_drag_cd
                          * params_.frontal_area_m2 * speed_ * speed_;
    if (speed_ < 0) F_drag_n = -F_drag_n;
    double drag_acc = F_drag_n / mass;

    // Rolling resistance: F_rr = Crr * m * g, opposing motion. Disabled below ~creep speed.
    double rr_acc = 0.0;
    if (abs_speed > 0.1)
    {
        rr_acc = params_.rolling_resistance_coeff * 9.81;  // m/s² magnitude
    }

    // Engine compression braking: physics-based formula
    //   F_brake = drag_torque(rpm) * total_ratio * eta / r
    // gives a real ~5x range between 1st and top gear, instead of the legacy
    // empirical scaler. Applied when off-throttle and converter is mostly
    // locked (otherwise drag wouldn't propagate back through the TC).
    double engine_brake_acc = 0.0;
    if (throttle < 0.05 && at_out.range != AutoTransmission::Range::NEUTRAL && slip_factor > 0.5)
    {
        double drag_nm = params_.engine_drag_base_nm
                       + params_.engine_drag_per_krpm * (rpm_ / 1000.0);
        engine_brake_acc = drag_nm * std::abs(total_ratio) * eta / r / mass;
    }

    double acc = engine_acc;
    if (speed_ > 0.01)        acc -= brake_acc + drag_acc + rr_acc + engine_brake_acc;
    else if (speed_ < -0.01)  acc += brake_acc + std::fabs(drag_acc) + rr_acc + engine_brake_acc;
    else if (brake > 0)       { acc = 0; speed_ = 0; }

    speed_ += acc * dt;
    if (speed_ < -20.0) speed_ = -20.0;
    // Note: deliberately no max_speed clamp here. The drag/RR balance is what
    // physically caps top speed; a hard clamp would mask power-band issues.

    StepLateralAndAttitude(dt, steering, acc);
}

} // namespace gt_esmini
