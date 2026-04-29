/*
 * GT_esmini - Extended esmini with HostVehicleData Functionality
 * https://github.com/esmini/esmini
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2024 GT_esmini contributors
 */

#include "gt_esmini/osi/HVDEstimator.hpp"
#include "Entities.hpp"
#include "RoadManager.hpp"
#include "CommonMini.hpp"
#include "gt_esmini/scenario/ExtraEntities.hpp"

#include <cmath>
#include <cstdio>
#include <algorithm>
#include <fstream>
#include <sstream>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace gt_esmini
{

namespace
{
// Minimal JSON helpers: locate "key" within a brace-delimited block and parse
// either a scalar number or an array of numbers. Mirrors the line-based
// parsing pattern used by VehiclePhysicsManager::LoadProfiles.

std::string ExtractBlock(const std::string& text, size_t start)
{
    if (start >= text.size() || text[start] != '{')
    {
        return "";
    }
    int depth = 0;
    size_t end = start;
    for (; end < text.size(); ++end)
    {
        if (text[end] == '{') depth++;
        else if (text[end] == '}') { depth--; if (depth == 0) break; }
    }
    return text.substr(start, end - start + 1);
}

double ParseDouble(const std::string& block, const std::string& key, double fallback)
{
    size_t pos = block.find("\"" + key + "\"");
    if (pos == std::string::npos) return fallback;
    size_t colon = block.find(':', pos);
    if (colon == std::string::npos) return fallback;
    try { return std::stod(block.substr(colon + 1)); }
    catch (...) { return fallback; }
}

std::vector<double> ParseDoubleArray(const std::string& block, const std::string& key,
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
        catch (...) { /* skip */ }
    }
    return out.empty() ? fallback : out;
}

std::string FindSection(const std::string& content, const std::string& key)
{
    size_t pos = content.find("\"" + key + "\"");
    if (pos == std::string::npos) return "";
    size_t lb = content.find('{', pos);
    if (lb == std::string::npos) return "";
    return ExtractBlock(content, lb);
}

double SmoothStep01(double x)
{
    x = std::clamp(x, 0.0, 1.0);
    return x * x * (3.0 - 2.0 * x);
}

std::string ParseString(const std::string& block, const std::string& key,
                        const std::string& fallback)
{
    size_t pos = block.find("\"" + key + "\"");
    if (pos == std::string::npos) return fallback;
    size_t colon = block.find(':', pos);
    if (colon == std::string::npos) return fallback;
    size_t q1 = block.find('"', colon + 1);
    if (q1 == std::string::npos) return fallback;
    size_t q2 = block.find('"', q1 + 1);
    if (q2 == std::string::npos) return fallback;
    return block.substr(q1 + 1, q2 - q1 - 1);
}
} // namespace

const HVDEstimator::ShiftParams& HVDEstimator::Active() const
{
    auto it = mode_shift_params_.find(active_mode_);
    if (it != mode_shift_params_.end()) return it->second;
    static const ShiftParams kDefault;
    return kDefault;
}

HVDEstimator::ShiftParams& HVDEstimator::Active()
{
    return mode_shift_params_[active_mode_];
}

bool HVDEstimator::SetActiveMode(const std::string& mode)
{
    if (mode_shift_params_.find(mode) == mode_shift_params_.end())
    {
        printf("HVDEstimator: unknown drive mode '%s', ignored\n", mode.c_str());
        return false;
    }
    if (active_mode_ == mode) return true;
    active_mode_ = mode;
    // Reset hold/event timers so the new schedule takes effect promptly,
    // but preserve gear/RPM state to avoid jumps.
    for (auto& kv : cache_)
    {
        kv.second.gear_hold_timer = 0.0;
        kv.second.shift_event_timer = 0.0;
        kv.second.shift_direction = 0;
    }
    printf("HVDEstimator: drive mode set to '%s'\n", mode.c_str());
    return true;
}

void HVDEstimator::LoadParams(const std::string& configPath)
{
    std::ifstream file(configPath);
    if (!file.is_open())
    {
        params_loaded_ = true;  // fall back to defaults; don't retry every frame
        return;
    }
    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    file.close();

    std::string ped = FindSection(content, "pedal_estimation");
    if (!ped.empty())
    {
        pedal_params_.mass_kg            = ParseDouble(ped, "mass_kg",            pedal_params_.mass_kg);
        pedal_params_.drag_coeff         = ParseDouble(ped, "drag_coeff",         pedal_params_.drag_coeff);
        pedal_params_.frontal_area_m2    = ParseDouble(ped, "frontal_area_m2",    pedal_params_.frontal_area_m2);
        pedal_params_.air_density        = ParseDouble(ped, "air_density",        pedal_params_.air_density);
        pedal_params_.rolling_resistance = ParseDouble(ped, "rolling_resistance_coeff", pedal_params_.rolling_resistance);
        pedal_params_.engine_brake_decel = ParseDouble(ped, "engine_brake_decel_mps2",  pedal_params_.engine_brake_decel);
        pedal_params_.engine_brake_gear_factor =
            ParseDoubleArray(ped, "engine_brake_gear_factor", pedal_params_.engine_brake_gear_factor);
    }

    std::string shf = FindSection(content, "shift_schedule");
    if (!shf.empty())
    {
        // Common (top-level) values shared across all modes.
        ShiftParams base;
        base.gear_ratios       = ParseDoubleArray(shf, "gear_ratios", base.gear_ratios);
        base.final_drive_ratio = ParseDouble(shf, "final_drive_ratio", base.final_drive_ratio);

        // Helper: fill a ShiftParams from a JSON block, falling back to base.
        auto fill = [&](const std::string& blk, ShiftParams& sp) {
            sp.gear_ratios               = base.gear_ratios;
            sp.final_drive_ratio         = base.final_drive_ratio;
            sp.shift_up_kmh              = ParseDoubleArray(blk, "shift_up_kmh", sp.shift_up_kmh);
            sp.shift_down_kmh            = ParseDoubleArray(blk, "shift_down_kmh", sp.shift_down_kmh);
            sp.kickdown_gain             = ParseDouble(blk, "kickdown_gain", sp.kickdown_gain);
            sp.brake_downshift_threshold = ParseDouble(blk, "brake_downshift_threshold", sp.brake_downshift_threshold);
            sp.min_gear_hold_s           = ParseDouble(blk, "min_gear_hold_s", sp.min_gear_hold_s);
            sp.rpm_tau_s                 = ParseDouble(blk, "rpm_tau_s", sp.rpm_tau_s);
            sp.v_lockup_mps              = ParseDouble(blk, "v_lockup_mps", sp.v_lockup_mps);
            sp.shift_event_duration_s    = ParseDouble(blk, "shift_event_duration_s", sp.shift_event_duration_s);
            sp.upshift_dip_rpm           = ParseDouble(blk, "upshift_dip_rpm", sp.upshift_dip_rpm);
            sp.downshift_blip_rpm        = ParseDouble(blk, "downshift_blip_rpm", sp.downshift_blip_rpm);
            sp.shift_torque_factor       = ParseDouble(blk, "shift_torque_factor", sp.shift_torque_factor);
            sp.rpm_tau_up_s              = ParseDouble(blk, "rpm_tau_up_s", sp.rpm_tau_up_s);
            sp.rpm_tau_down_s            = ParseDouble(blk, "rpm_tau_down_s", sp.rpm_tau_down_s);
        };

        mode_shift_params_.clear();

        // Parse "modes" section if present.
        std::string modes = FindSection(shf, "modes");
        if (!modes.empty())
        {
            // Walk top-level keys inside "modes": each is a mode name with a block value.
            size_t cursor = 1;  // skip leading '{'
            while (cursor < modes.size())
            {
                size_t q1 = modes.find('"', cursor);
                if (q1 == std::string::npos) break;
                size_t q2 = modes.find('"', q1 + 1);
                if (q2 == std::string::npos) break;
                std::string mode_name = modes.substr(q1 + 1, q2 - q1 - 1);
                size_t lb = modes.find('{', q2);
                if (lb == std::string::npos) break;
                std::string block = ExtractBlock(modes, lb);
                if (block.empty()) break;
                ShiftParams sp;
                fill(block, sp);
                mode_shift_params_[mode_name] = sp;
                cursor = lb + block.size();
            }
        }

        // Backward compatibility: if no modes parsed, accept legacy top-level
        // shift_schedule keys as the "comfort" profile.
        if (mode_shift_params_.empty())
        {
            ShiftParams sp;
            fill(shf, sp);
            mode_shift_params_["comfort"] = sp;
        }

        // Active mode selection: prefer JSON-specified, otherwise keep current.
        std::string desired = ParseString(shf, "active_mode", active_mode_);
        if (mode_shift_params_.find(desired) != mode_shift_params_.end())
        {
            active_mode_ = desired;
        }
        else if (mode_shift_params_.find(active_mode_) == mode_shift_params_.end())
        {
            // Fallback to the first available mode.
            active_mode_ = mode_shift_params_.begin()->first;
        }
    }

    params_loaded_ = true;
}

int HVDEstimator::SeedInitialGear(double speed_kmh) const
{
    const auto& sp = Active();
    int gear = 1;
    const auto& up = sp.shift_up_kmh;
    const auto& dn = sp.shift_down_kmh;
    int n_thresh = static_cast<int>(std::min(up.size(), dn.size()));
    for (int g = 0; g < n_thresh; ++g)
    {
        double mid = 0.5 * (up[g] + dn[g]);
        if (speed_kmh >= mid)
        {
            gear = g + 2;  // shift_up_kmh[g] is the upshift point from gear g+1 to g+2
        }
    }
    int max_gear = static_cast<int>(sp.gear_ratios.size());
    return std::clamp(gear, 1, max_gear);
}

int HVDEstimator::SelectGear(double speed_kmh, double throttle, double brake,
                              VehicleCache& vc, double dt) const
{
    const auto& sp = Active();
    int g = vc.current_gear;
    int max_gear = static_cast<int>(sp.gear_ratios.size());
    g = std::clamp(g, 1, max_gear);

    vc.gear_hold_timer = std::max(0.0, vc.gear_hold_timer - dt);
    if (vc.gear_hold_timer > 0.0)
    {
        return g;
    }

    const auto& up = sp.shift_up_kmh;
    const auto& dn = sp.shift_down_kmh;
    int n_up = static_cast<int>(up.size());
    int n_dn = static_cast<int>(dn.size());

    // Throttle-aware kickdown: deeper throttle delays upshift and triggers
    // downshift earlier (higher RPM band).
    double kickdown = 1.0 + throttle * sp.kickdown_gain;

    // Upshift check
    if (g <= n_up && g < max_gear)
    {
        double thr = up[g - 1] * kickdown;
        if (speed_kmh > thr)
        {
            vc.current_gear = g + 1;
            vc.gear_hold_timer = sp.min_gear_hold_s;
            return vc.current_gear;
        }
    }

    // Downshift check
    if (g >= 2 && g - 1 <= n_dn)
    {
        double base = dn[g - 2] * kickdown;
        // Brake-induced earlier downshift (engine braking)
        if (brake > sp.brake_downshift_threshold)
        {
            base *= 1.20;
        }
        if (speed_kmh < base)
        {
            vc.current_gear = g - 1;
            vc.gear_hold_timer = sp.min_gear_hold_s;
            return vc.current_gear;
        }
    }

    return g;
}

double HVDEstimator::EstimateRPM(double abs_speed, int gear, double dt, VehicleCache& vc) const
{
    const auto& sp = Active();
    int max_gear = static_cast<int>(sp.gear_ratios.size());
    int gi = std::clamp(gear, 1, max_gear) - 1;
    double total_ratio = sp.gear_ratios[gi] * sp.final_drive_ratio;

    double wheel_rps  = abs_speed / (2.0 * M_PI * kWheelRadius);
    double geared_rpm = wheel_rps * 60.0 * total_ratio;

    // Low-speed lockup interpolation (clutch/torque-converter slip).
    // Below v_lockup, blend toward idle RPM. Above, fully geared.
    double v_lockup = std::max(0.1, sp.v_lockup_mps);
    double slip = SmoothStep01(abs_speed / v_lockup);
    double target_rpm = kIdleRPM + slip * (geared_rpm - kIdleRPM);

    // Shift-event transient: dip on upshift, rev-match blip on downshift.
    // Magnitude scales linearly with remaining timer (fades to zero at end).
    double tau = std::max(1e-3, sp.rpm_tau_s);
    if (vc.shift_event_timer > 0.0 && sp.shift_event_duration_s > 0.0)
    {
        double phase = std::clamp(vc.shift_event_timer / sp.shift_event_duration_s, 0.0, 1.0);
        if (vc.shift_direction > 0)
        {
            target_rpm -= sp.upshift_dip_rpm * phase;
            tau = std::max(1e-3, sp.rpm_tau_up_s);
        }
        else if (vc.shift_direction < 0)
        {
            target_rpm += sp.downshift_blip_rpm * phase;
            tau = std::max(1e-3, sp.rpm_tau_down_s);
        }
        vc.shift_event_timer = std::max(0.0, vc.shift_event_timer - dt);
        if (vc.shift_event_timer == 0.0) vc.shift_direction = 0;
    }
    target_rpm = std::clamp(target_rpm, kIdleRPM, kMaxRPM);

    // 1st-order lag (engine inertia): rpm += alpha * (target - rpm)
    double alpha = (dt > 0.0) ? (dt / (tau + dt)) : 1.0;

    double rpm;
    if (vc.initialized)
    {
        rpm = vc.prev_rpm + alpha * (target_rpm - vc.prev_rpm);
    }
    else
    {
        rpm = target_rpm;  // seed
    }
    vc.prev_rpm = rpm;
    return rpm;
}

HVDEstimator::EstimatedInputs HVDEstimator::Estimate(scenarioengine::Object* obj, double dt)
{
    EstimatedInputs result;
    if (!obj)
    {
        return result;
    }

    if (obj->type_ != scenarioengine::Object::Type::VEHICLE)
    {
        return result;
    }

    int    id        = obj->GetId();
    double speed     = obj->GetSpeed();
    double abs_speed = std::abs(speed);
    double speed_kmh = abs_speed * 3.6;

    auto& vc = cache_[id];
    const bool was_initialized = vc.initialized;

    // Seed initial gear / hold timer for mid-cruise scenario starts
    if (!was_initialized)
    {
        vc.current_gear   = SeedInitialGear(speed_kmh);
        vc.gear_hold_timer = Active().min_gear_hold_s;
        vc.prev_gear      = vc.current_gear;
    }

    // --- Pedal estimation (force-based inverse longitudinal dynamics) ---
    double a       = obj->pos_.GetAccLong();              // esmini-smoothed
    double pitch   = obj->pos_.GetPRoad();                // road grade [rad]
    double dir     = (speed >= 0.0) ? 1.0 : -1.0;
    double grade_sin = dir * std::sin(pitch);
    double grade_cos = std::cos(pitch);

    double max_acc = obj->GetMaxAcceleration();
    double max_dec = obj->GetMaxDeceleration();
    if (max_acc <= 0.0) max_acc = kDefaultMaxAcc;
    if (max_dec <= 0.0) max_dec = kDefaultMaxDec;

    const double m   = pedal_params_.mass_kg;
    const double rho = pedal_params_.air_density;
    const double Cd  = pedal_params_.drag_coeff;
    const double A   = pedal_params_.frontal_area_m2;
    const double Crr = pedal_params_.rolling_resistance;

    double F_drag  = 0.5 * rho * Cd * A * abs_speed * abs_speed;
    double F_roll  = (abs_speed > 0.05) ? (Crr * m * kGravity * grade_cos) : 0.0;
    double F_grade = m * kGravity * grade_sin;
    double F_required = m * a + F_drag + F_roll + F_grade;

    double F_engine_max = m * max_acc;
    double F_brake_max  = m * max_dec;

    int max_gear_count = static_cast<int>(Active().gear_ratios.size());
    int gi = std::clamp(vc.current_gear, 1, max_gear_count) - 1;
    double eb_factor = (gi < static_cast<int>(pedal_params_.engine_brake_gear_factor.size()))
                           ? pedal_params_.engine_brake_gear_factor[gi]
                           : 1.0;
    double F_engine_brake = eb_factor * m * pedal_params_.engine_brake_decel;

    if (abs_speed < kSpeedThreshold)
    {
        // Standstill: hold brake, neutral
        result.throttle = 0.0;
        result.brake    = 1.0;
    }
    else if (F_required > 0.0)
    {
        result.throttle = std::clamp(F_required / F_engine_max, 0.0, 1.0);
        result.brake    = 0.0;
    }
    else if (F_required > -F_engine_brake)
    {
        // Coast (engine braking deadzone)
        result.throttle = 0.0;
        result.brake    = 0.0;
    }
    else
    {
        result.brake    = std::clamp((-F_required - F_engine_brake) / F_brake_max, 0.0, 1.0);
        result.throttle = 0.0;
    }

    // --- Pedal smoothing (EMA) ---
    if (was_initialized)
    {
        result.throttle = kPedalSmoothAlpha * result.throttle + (1.0 - kPedalSmoothAlpha) * vc.prev_throttle;
        result.brake    = kPedalSmoothAlpha * result.brake + (1.0 - kPedalSmoothAlpha) * vc.prev_brake;
    }
    vc.prev_throttle = result.throttle;
    vc.prev_brake    = result.brake;

    // --- Gear selection (forward only; reverse/neutral handled at output) ---
    int forward_gear = SelectGear(speed_kmh, result.throttle, result.brake, vc, dt);

    // Detect a gear change to start a shift-event window (RPM dip / blip + torque cut).
    if (was_initialized && forward_gear != vc.prev_gear)
    {
        const auto& sp_e = Active();
        if (sp_e.shift_event_duration_s > 0.0)
        {
            vc.shift_event_timer = sp_e.shift_event_duration_s;
            vc.shift_direction   = (forward_gear > vc.prev_gear) ? 1 : -1;
        }
    }
    vc.prev_gear = forward_gear;

    // Direction state machine: D and R are held through standstill; N is only
    // inserted briefly when the motion direction reverses (D<->R transition).
    int raw_dir = (speed >  kSpeedThreshold) ?  1
                : (speed < -kSpeedThreshold) ? -1
                                              :  0;

    if (!was_initialized)
    {
        vc.reported_direction = (raw_dir != 0) ? raw_dir : 1;  // default to D
        vc.neutral_hold_timer = 0.0;
    }

    if (vc.neutral_hold_timer > 0.0)
    {
        vc.neutral_hold_timer = std::max(0.0, vc.neutral_hold_timer - dt);
        if (vc.neutral_hold_timer == 0.0 && raw_dir != 0)
        {
            vc.reported_direction = raw_dir;
        }
        // during hold, reported_direction stays 0 (N)
    }
    else if (raw_dir == 0)
    {
        // Standstill: hold last reported direction (D stays D, R stays R)
    }
    else if (vc.reported_direction != 0 && raw_dir != vc.reported_direction)
    {
        // Direction reversal detected: insert N for a brief hold
        vc.reported_direction = 0;
        vc.neutral_hold_timer = kNeutralTransitionHold;
    }
    else
    {
        vc.reported_direction = raw_dir;
    }

    if (vc.reported_direction == -1)
    {
        result.gear = -1;
    }
    else if (vc.reported_direction == 0)
    {
        result.gear = 0;
    }
    else
    {
        result.gear = forward_gear;
    }

    // --- Steering (hybrid: heading-rate + preview attenuation) ---
    {
        double raw_rate = obj->GetWheelAngle();
        double max_steer = obj->front_axle_.maxSteering;

        double preview_dist = std::clamp(abs_speed * kPreviewTime,
                                         kPreviewDistMin, kPreviewDistMax);
        roadmanager::Position preview_pos = obj->pos_;
        preview_pos.MoveAlongS(preview_dist);
        double road_error = GetAngleDifference(preview_pos.GetH(), obj->pos_.GetH());
        double wheelbase  = obj->front_axle_.positionX;
        double preview_steer = atan2(road_error * wheelbase, preview_dist);

        double raw_steering = raw_rate;
        if (std::abs(raw_rate) > 0.02)
        {
            double ratio = std::clamp(std::abs(preview_steer) / std::abs(raw_rate), 0.0, 1.0);
            raw_steering = raw_rate * ratio;
        }

        raw_steering = std::clamp(raw_steering, -max_steer, max_steer);

        if (was_initialized)
        {
            result.steering = kSteerEmaAlpha * raw_steering
                            + (1.0 - kSteerEmaAlpha) * vc.prev_steering;
        }
        else
        {
            result.steering = raw_steering;
        }
        vc.prev_steering = result.steering;
    }

    // --- RPM (target from gear+speed, lockup blending, 1st-order lag) ---
    int rpm_gear = (result.gear > 0) ? result.gear : forward_gear;
    result.rpm = EstimateRPM(abs_speed, rpm_gear, dt, vc);

    result.torque    = EstimateTorque(result.rpm);
    // Torque interrupt during shift event (visualizes the "torque hole").
    if (vc.shift_event_timer > 0.0)
    {
        result.torque *= Active().shift_torque_factor;
    }
    result.lightMask = BuildLightMaskForObject(obj);

    vc.prev_speed  = speed;
    vc.initialized = true;

    return result;
}

double HVDEstimator::EstimateTorque(double rpm) const
{
    double normalized_rpm = (rpm - kIdleRPM) / (kMaxRPM - kIdleRPM);
    normalized_rpm        = std::clamp(normalized_rpm, 0.0, 1.0);

    double half_width = std::max(kTorquePeakPos, 1.0 - kTorquePeakPos);
    double d = (normalized_rpm - kTorquePeakPos) / half_width;
    double shape = std::max(0.0, 1.0 - d * d);

    return kTorqueMin + (1.0 - kTorqueMin) * shape;
}

int HVDEstimator::BuildLightMaskForObject(scenarioengine::Object* obj)
{
    if (!obj)
    {
        return 0;
    }

    auto* vehicle = dynamic_cast<scenarioengine::Vehicle*>(obj);
    if (!vehicle)
    {
        return 0;
    }

    auto* ext = VehicleExtensionManager::Instance().GetExtension(vehicle);
    if (!ext)
    {
        return 0;
    }

    auto is_on = [&](VehicleLightType type) -> bool {
        return ext->GetLightState(type).mode == LightState::Mode::ON;
    };

    int mask = 0;
    if (is_on(VehicleLightType::LOW_BEAM))         mask |= 1;
    if (is_on(VehicleLightType::HIGH_BEAM))        mask |= 2;
    if (is_on(VehicleLightType::INDICATOR_LEFT))   mask |= 4;
    if (is_on(VehicleLightType::INDICATOR_RIGHT))  mask |= 8;
    if (is_on(VehicleLightType::FOG_LIGHTS) || is_on(VehicleLightType::FOG_LIGHTS_FRONT) ||
        is_on(VehicleLightType::FOG_LIGHTS_REAR))
    {
        mask |= 16;
    }
    return mask;
}

void HVDEstimator::Reset()
{
    cache_.clear();
}

} // namespace gt_esmini
