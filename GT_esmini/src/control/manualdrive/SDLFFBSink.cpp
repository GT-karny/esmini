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

    spring_coefficient_ = config.ffb.spring_coefficient;
    damper_coefficient_ = config.ffb.damper_coefficient;
    constant_gain_      = config.ffb.constant_gain;
    max_force_          = config.ffb.max_force;

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

    double speed_factor = std::min(speed / 30.0, 1.0);

    if (emulate_via_constant_)
    {
        // Combine all forces into single constant effect for devices like G29
        double steering_vel = (steering_pos - prev_steering_) / std::max(dt, 0.001);
        prev_steering_ = steering_pos;
        speed_for_ps_ = speed;

        double sat_force = -lat_accel * constant_gain_ * speed_factor;  // SAT grows with speed
        double spring_coeff = spring_coefficient_;  // Not speed-scaled (mild constant centering)
        double damper_coeff = damper_coefficient_;

        UpdateCombinedConstantForce(sat_force, spring_coeff, damper_coeff,
                                    steering_pos, steering_vel);
        return;
    }

    // Native effects path
    if (has_constant_ && constant_effect_id_ >= 0)
    {
        double force = -lat_accel * constant_gain_;
        force = std::clamp(force, -max_force_, max_force_);
        UpdateConstantEffect(force);
    }

    if (has_spring_ && spring_effect_id_ >= 0)
    {
        double coeff = spring_coefficient_ * speed_factor;
        UpdateSpringEffect(coeff);
    }

    if (has_damper_ && damper_effect_id_ >= 0)
    {
        double coeff = damper_coefficient_ * speed_factor;
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

void SDLFFBSink::UpdateCombinedConstantForce(double sat_force, double spring_coeff, double damper_coeff,
                                              double steering_pos, double steering_vel)
{
    // === FFB Model v4 ===
    //
    // Two components only:
    //   1. Friction: Coulomb model, opposes steering motion. U-shaped vs speed.
    //   2. Centering: constant-magnitude force toward center. Speed-dependent magnitude.
    //      NOT proportional to steering angle — just a fixed push toward zero.
    //
    // Speed behavior:
    //   0 m/s:   Heavy friction, no centering. Wheel stays put.
    //   ~5 km/h: Friction drops, centering appears → wheel returns to center on its own.
    //   8+ m/s:  Light friction, moderate centering.
    //   30 m/s:  Heavier friction (stability), strong centering + SAT.

    double v = speed_for_ps_;

    // --- 1. Friction (Coulomb) ---
    // U-shaped vs speed: heavy at stop, light mid-speed, heavier at high speed.
    // Opposes steering motion direction (not angle).
    double friction_mag;
    if (v < 8.0)
    {
        friction_mag = 0.50 - (0.50 - 0.05) * (v / 8.0);  // 0.50 at stop → 0.05 at 8 m/s
    }
    else
    {
        friction_mag = 0.05 + (0.15 - 0.05) * std::min((v - 8.0) / 22.0, 1.0);  // → 0.15 at 30 m/s
    }

    // Reduce friction when returning to center so centering force wins
    bool returning = (steering_pos * steering_vel) < 0.0;
    double off_center = std::clamp(std::abs(steering_pos) / 0.087, 0.0, 1.0);
    double friction_scale = returning ? (1.0 - 0.70 * off_center) : 1.0;

    // Smooth Coulomb: linear near zero velocity to prevent chattering
    double friction = -std::tanh(steering_vel * 2.0) * friction_mag * friction_scale;

    // --- 2. Centering (speed-dependent constant torque toward center) ---
    // Magnitude depends on speed ONLY, NOT on steering angle.
    // At a given speed the centering force is constant regardless of how far
    // the wheel is turned — tanh is used only for smooth direction switching.
    double centering_onset = std::clamp(v / 4.0, 0.0, 1.0);
    centering_onset = 0.10 + 0.90 * centering_onset;  // 0.10 at stop, 1.0 at 4+ m/s
    double high_speed_boost = 1.0 + 0.5 * std::clamp((v - 15.0) / 15.0, 0.0, 1.0);
    double centering_mag = spring_coeff * centering_onset * high_speed_boost;

    // Smooth sign switch: reaches ±0.93 by 0.2 rad (11°), ±0.99 by 0.35 rad (20°)
    // Below ~11° the force ramps smoothly — no wall, no angle-proportional feel
    double centering = -std::tanh(steering_pos * 8.0) * centering_mag;

    // --- 3. SAT (self-aligning torque) ---
    double sat = sat_force;

    // Combine
    double total = friction + centering + sat;
    total = std::clamp(total, -max_force_, max_force_);

    static int log_counter = 0;
    if (++log_counter % 100 == 0)
    {
        LOG_INFO("SDLFFBSink: total={:.3f} (fric={:.3f} center={:.3f} sat={:.3f} v={:.1f})",
                 total, friction, centering, sat, v);
    }

    UpdateConstantEffect(total);
}

} // namespace gt_esmini

#endif // GT_ENABLE_SDL2
