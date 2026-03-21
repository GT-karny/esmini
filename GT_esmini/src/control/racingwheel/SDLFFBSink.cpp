#ifdef GT_ENABLE_SDL2

#include "gt_esmini/control/racingwheel/SDLFFBSink.hpp"
#include "gt_esmini/control/racingwheel/RacingWheelConfig.hpp"
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

bool SDLFFBSink::Init(SDL_Joystick* joystick, const RacingWheelConfig& config)
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
        // Initial coefficient — will be updated each frame
        effect.condition.right_coeff[0] = 0;
        effect.condition.left_coeff[0] = 0;
        effect.condition.right_sat[0] = 0x7FFF;
        effect.condition.left_sat[0] = 0x7FFF;
        spring_effect_id_ = SDL_HapticNewEffect(haptic_, &effect);
        if (spring_effect_id_ >= 0)
        {
            SDL_HapticRunEffect(haptic_, spring_effect_id_, 1);
        }
        else
        {
            LOG_WARN("SDLFFBSink: Failed to create spring effect: {}", SDL_GetError());
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
        if (damper_effect_id_ >= 0)
        {
            SDL_HapticRunEffect(haptic_, damper_effect_id_, 1);
        }
        else
        {
            LOG_WARN("SDLFFBSink: Failed to create damper effect: {}", SDL_GetError());
        }
    }

    return true;
}

void SDLFFBSink::Update(const osi3::HostVehicleData& hvd, double /*dt*/)
{
    if (!haptic_ || !enabled_)
    {
        return;
    }

    // Extract speed from HVD location
    double speed = 0.0;
    double lat_accel = 0.0;

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

    // Self-aligning torque: proportional to lateral acceleration
    if (has_constant_ && constant_effect_id_ >= 0)
    {
        double force = -lat_accel * constant_gain_;
        force = std::clamp(force, -max_force_, max_force_);
        UpdateConstantEffect(force);
    }

    // Spring centering: coefficient increases with speed
    if (has_spring_ && spring_effect_id_ >= 0)
    {
        double coeff = spring_coefficient_ * std::min(speed / 30.0, 1.0);
        UpdateSpringEffect(coeff);
    }

    // Damper resistance: increases with speed
    if (has_damper_ && damper_effect_id_ >= 0)
    {
        double coeff = damper_coefficient_ * std::min(speed / 30.0, 1.0);
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

} // namespace gt_esmini

#endif // GT_ENABLE_SDL2
