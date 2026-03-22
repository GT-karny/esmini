#ifdef GT_ENABLE_SDL2

#include "gt_esmini/control/manualdrive/SDLFFBSink.hpp"
#include "gt_esmini/control/manualdrive/ManualDriveConfig.hpp"
#include "CommonMini.hpp"
#include "logger.hpp"

#include <cmath>
#include <algorithm>

namespace gt_esmini
{

SDLFFBSink::SDLFFBSink() = default;

SDLFFBSink::~SDLFFBSink()
{
    Close();
}

bool SDLFFBSink::Init(SDL_Joystick* joystick, const ManualDriveConfig& config)
{
    if (!joystick)
    {
        return false;
    }

    sat_gain_            = config.ffb.sat_gain;
    sat_centering_gain_  = config.ffb.sat_centering_gain;
    friction_base_       = config.ffb.friction_base;
    friction_speed_gain_ = config.ffb.friction_speed_gain;
    damper_base_         = config.ffb.damper_base;
    damper_speed_gain_   = config.ffb.damper_speed_gain;
    soft_stop_gain_      = config.ffb.soft_stop_gain;
    lock_angle_          = config.ffb.lock_angle;
    assist_low_speed_    = config.ffb.assist_low_speed;
    assist_high_speed_   = config.ffb.assist_high_speed;
    max_force_           = config.ffb.max_force;

    LOG_INFO("SDLFFBSink: Config loaded — sat_gain={:.3f} centering={:.3f} fric_base={:.3f} fric_spd={:.3f} "
             "damp_base={:.3f} damp_spd={:.3f} assist_lo={:.2f} assist_hi={:.2f} max_force={:.2f}",
             sat_gain_, sat_centering_gain_, friction_base_, friction_speed_gain_,
             damper_base_, damper_speed_gain_, assist_low_speed_, assist_high_speed_, max_force_);

    if (!SDL_JoystickIsHaptic(joystick))
    {
        LOG_INFO("SDLFFBSink: Joystick does not support haptic feedback");
        return false;
    }

    haptic_ = SDL_HapticOpenFromJoystick(joystick);
    if (!haptic_)
    {
        LOG_WARN("SDLFFBSink: Failed to open haptic: {}", SDL_GetError());
        return false;
    }

    // Query device capabilities
    unsigned int caps = SDL_HapticQuery(haptic_);
    has_constant_ = (caps & SDL_HAPTIC_CONSTANT) != 0;
    has_spring_   = (caps & SDL_HAPTIC_SPRING) != 0;
    has_damper_   = (caps & SDL_HAPTIC_DAMPER) != 0;

    LOG_INFO("SDLFFBSink: Haptic opened — constant={}, spring={}, damper={}",
             has_constant_, has_spring_, has_damper_);

    // Create constant force effect (self-aligning torque)
    if (has_constant_)
    {
        SDL_HapticEffect effect = {};
        effect.type = SDL_HAPTIC_CONSTANT;
        effect.constant.direction.type = SDL_HAPTIC_CARTESIAN;
        effect.constant.direction.dir[0] = 1;  // X-axis (steering)
        effect.constant.length = SDL_HAPTIC_INFINITY;
        effect.constant.level = 0;
        effect.constant.attack_length = 0;
        effect.constant.fade_length = 0;
        constant_effect_id_ = SDL_HapticNewEffect(haptic_, &effect);
        if (constant_effect_id_ >= 0)
        {
            SDL_HapticRunEffect(haptic_, constant_effect_id_, 1);
        }
        else
        {
            LOG_WARN("SDLFFBSink: Failed to create constant effect: {}", SDL_GetError());
        }
    }

    // Create spring effect (centering)
    if (has_spring_)
    {
        SDL_HapticEffect effect = {};
        effect.type = SDL_HAPTIC_SPRING;
        effect.condition.length = SDL_HAPTIC_INFINITY;
        effect.condition.right_coeff[0] = 0;
        effect.condition.left_coeff[0] = 0;
        effect.condition.right_sat[0] = 0x7FFF;
        effect.condition.left_sat[0] = 0x7FFF;
        spring_effect_id_ = SDL_HapticNewEffect(haptic_, &effect);
        if (spring_effect_id_ < 0)
        {
            LOG_WARN("SDLFFBSink: Spring effect unsupported: {}", SDL_GetError());
            has_spring_ = false;
        }
        else
        {
            SDL_HapticRunEffect(haptic_, spring_effect_id_, 1);
        }
    }

    // Create damper effect (steering resistance)
    if (has_damper_)
    {
        SDL_HapticEffect effect = {};
        effect.type = SDL_HAPTIC_DAMPER;
        effect.condition.length = SDL_HAPTIC_INFINITY;
        effect.condition.right_coeff[0] = 0;
        effect.condition.left_coeff[0] = 0;
        effect.condition.right_sat[0] = 0x7FFF;
        effect.condition.left_sat[0] = 0x7FFF;
        damper_effect_id_ = SDL_HapticNewEffect(haptic_, &effect);
        if (damper_effect_id_ < 0)
        {
            LOG_WARN("SDLFFBSink: Damper effect unsupported: {}", SDL_GetError());
            has_damper_ = false;
        }
        else
        {
            SDL_HapticRunEffect(haptic_, damper_effect_id_, 1);
        }
    }

    // If spring/damper failed but constant works, emulate via constant force
    if (has_constant_ && constant_effect_id_ >= 0 && (!has_spring_ || !has_damper_))
    {
        emulate_via_constant_ = true;
        LOG_INFO("SDLFFBSink: Emulating spring/damper via constant force (G29 compatible)");
    }

    return true;
}

void SDLFFBSink::Update(const osi3::HostVehicleData& hvd, double dt)
{
    if (!haptic_ || !enabled_)
    {
        return;
    }

    // Extract vehicle state from HVD
    double speed = 0.0;
    double lat_accel = 0.0;
    double steering_pos = 0.0;

    if (hvd.has_location())
    {
        const auto& loc = hvd.location();
        if (loc.has_velocity())
        {
            double vx = loc.velocity().x();
            double vy = loc.velocity().y();
            speed = std::sqrt(vx * vx + vy * vy);
        }
        if (loc.has_acceleration())
        {
            lat_accel = loc.acceleration().y();
        }
    }
    if (hvd.has_vehicle_steering() && hvd.vehicle_steering().has_vehicle_steering_wheel())
    {
        steering_pos = hvd.vehicle_steering().vehicle_steering_wheel().angle();
    }

    if (emulate_via_constant_)
    {
        double steering_vel = (steering_pos - prev_steering_) / std::max(dt, 0.001);
        prev_steering_ = steering_pos;

        UpdateCombinedConstantForce(lat_accel, speed, steering_pos, steering_vel);
        return;
    }

    // Native effects path — use SAT via constant, spring/damper as available
    double speed_factor = std::clamp(speed / 30.0, 0.0, 1.0);

    if (has_constant_ && constant_effect_id_ >= 0)
    {
        double assist_ratio = assist_low_speed_ + (assist_high_speed_ - assist_low_speed_) * speed_factor;
        double force = -lat_accel * sat_gain_ * (1.0 - assist_ratio);
        force = std::clamp(force, -max_force_, max_force_);
        UpdateConstantEffect(force);
    }

    if (has_spring_ && spring_effect_id_ >= 0)
    {
        double coeff = friction_base_ + friction_speed_gain_ * speed_factor;
        UpdateSpringEffect(coeff);
    }

    if (has_damper_ && damper_effect_id_ >= 0)
    {
        double coeff = damper_base_ + damper_speed_gain_ * speed_factor;
        UpdateDamperEffect(coeff);
    }
}

void SDLFFBSink::SetEnabled(bool enabled)
{
    enabled_ = enabled;
    if (!enabled && haptic_)
    {
        SDL_HapticStopAll(haptic_);
    }
}

void SDLFFBSink::Close()
{
    if (haptic_)
    {
        SDL_HapticStopAll(haptic_);
        if (constant_effect_id_ >= 0)
        {
            SDL_HapticDestroyEffect(haptic_, constant_effect_id_);
            constant_effect_id_ = -1;
        }
        if (spring_effect_id_ >= 0)
        {
            SDL_HapticDestroyEffect(haptic_, spring_effect_id_);
            spring_effect_id_ = -1;
        }
        if (damper_effect_id_ >= 0)
        {
            SDL_HapticDestroyEffect(haptic_, damper_effect_id_);
            damper_effect_id_ = -1;
        }
        SDL_HapticClose(haptic_);
        haptic_ = nullptr;
    }
}

void SDLFFBSink::UpdateConstantEffect(double force)
{
    // force: -1.0 ~ 1.0
    Sint16 level = static_cast<Sint16>(std::clamp(force, -1.0, 1.0) * 32767.0);

    SDL_HapticEffect effect = {};
    effect.type = SDL_HAPTIC_CONSTANT;
    effect.constant.direction.type = SDL_HAPTIC_CARTESIAN;
    effect.constant.direction.dir[0] = 1;
    effect.constant.length = SDL_HAPTIC_INFINITY;
    effect.constant.level = level;

    SDL_HapticUpdateEffect(haptic_, constant_effect_id_, &effect);
}

void SDLFFBSink::UpdateSpringEffect(double coefficient)
{
    // coefficient: 0.0 ~ 1.0
    Uint16 coeff = static_cast<Uint16>(std::clamp(coefficient, 0.0, 1.0) * 32767.0);

    SDL_HapticEffect effect = {};
    effect.type = SDL_HAPTIC_SPRING;
    effect.condition.length = SDL_HAPTIC_INFINITY;
    effect.condition.right_coeff[0] = coeff;
    effect.condition.left_coeff[0]  = coeff;
    effect.condition.right_sat[0]   = 0x7FFF;
    effect.condition.left_sat[0]    = 0x7FFF;

    SDL_HapticUpdateEffect(haptic_, spring_effect_id_, &effect);
}

void SDLFFBSink::UpdateDamperEffect(double coefficient)
{
    Uint16 coeff = static_cast<Uint16>(std::clamp(coefficient, 0.0, 1.0) * 32767.0);

    SDL_HapticEffect effect = {};
    effect.type = SDL_HAPTIC_DAMPER;
    effect.condition.length = SDL_HAPTIC_INFINITY;
    effect.condition.right_coeff[0] = coeff;
    effect.condition.left_coeff[0]  = coeff;
    effect.condition.right_sat[0]   = 0x7FFF;
    effect.condition.left_sat[0]    = 0x7FFF;

    SDL_HapticUpdateEffect(haptic_, damper_effect_id_, &effect);
}

void SDLFFBSink::UpdateCombinedConstantForce(double lat_accel, double speed,
                                              double steering_pos, double steering_vel)
{
    // === FFB Model v5: Physics-Inspired ===
    //
    // Four components:
    //   1. SAT:      Self-aligning torque from lateral acceleration (replaces centering)
    //   2. Friction:  Coulomb friction — opposes steering motion (steering weight)
    //   3. Damping:   Velocity-proportional resistance (viscous)
    //   4. SoftStop:  Progressive resistance near steering lock
    //
    // No artificial centering force. Centering IS the SAT.

    double speed_factor = std::clamp(speed / 30.0, 0.0, 1.0);

    // --- 1. SAT (Self-Aligning Torque) ---
    // Two components:
    //   Predictive: steering angle → slip angle → Fy → SAT (immediate response)
    //   Reactive:   lat_accel as Fy/m proxy (delayed, carries grip-limit info)

    // Power assist: high assist at low speed (light parking), low at high speed (heavy, stable)
    double assist_ratio = assist_low_speed_ + (assist_high_speed_ - assist_low_speed_) * speed_factor;
    double manual_ratio = 1.0 - assist_ratio;

    // Caster trail centering: geometric effect from caster angle + mechanical trail.
    // Any forward motion + nonzero steering angle → restoring torque.
    // NOT affected by power assist (it's a geometric/tire effect, not column torque).
    // Gentle onset: begins at walking speed (~1 m/s), full effect by ~5 m/s.
    double caster_onset = std::clamp(speed / 5.0, 0.0, 1.0);
    double sat_predictive = -steering_pos * sat_centering_gain_ * caster_onset;

    // Reactive SAT: from actual lateral acceleration (richer dynamics, grip-limit lightening).
    double slip_proxy = std::clamp(std::abs(lat_accel) / 9.81, 0.0, 1.0);
    double trail_factor = std::max(0.0, 1.0 - slip_proxy * slip_proxy);
    double sat_reactive = -lat_accel * sat_gain_ * trail_factor * manual_ratio;

    double sat = sat_predictive + sat_reactive;

    // --- 2. Friction (Coulomb) ---
    // Opposes steering velocity in both directions — this is the "weight" of steering.
    // Increases slightly with speed for highway stability.
    double friction_mag = friction_base_ + friction_speed_gain_ * speed_factor;
    double friction = -std::tanh(steering_vel * 3.0) * friction_mag;

    // --- 3. Damping (viscous) ---
    // Velocity-proportional resistance. More damping at speed for stability.
    double damping_coeff = damper_base_ + damper_speed_gain_ * speed_factor;
    double damping = -steering_vel * damping_coeff;

    // --- 4. Soft Stop ---
    // Progressive resistance near steering lock to prevent hard slam.
    double soft_stop = 0.0;
    double stop_zone = 0.1;  // ramp-up zone width [rad]
    double overshoot = std::abs(steering_pos) - (lock_angle_ - stop_zone);
    if (overshoot > 0.0)
    {
        double normalized = std::clamp(overshoot / stop_zone, 0.0, 1.0);
        soft_stop = -std::copysign(normalized * normalized * soft_stop_gain_, steering_pos);
    }

    // Combine
    double total = sat + friction + damping + soft_stop;
    total = std::clamp(total, -max_force_, max_force_);

    static int log_counter = 0;
    if (++log_counter % 50 == 0 || log_counter <= 5)
    {
        LOG_INFO("SDLFFBSink v5: total={:.3f} (sat_p={:.3f} sat_r={:.3f} fric={:.3f} damp={:.3f} stop={:.3f}) steer={:.3f} lat_a={:.2f} v={:.1f}",
                 total, sat_predictive, sat_reactive, friction, damping, soft_stop, steering_pos, lat_accel, speed);
    }

    UpdateConstantEffect(total);
}

} // namespace gt_esmini

#endif // GT_ENABLE_SDL2
