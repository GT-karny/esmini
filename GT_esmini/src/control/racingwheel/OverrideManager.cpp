#include "gt_esmini/control/racingwheel/OverrideManager.hpp"
#include "gt_esmini/control/racingwheel/RacingWheelConfig.hpp"

#include <cmath>

namespace gt_esmini
{

void OverrideManager::Configure(const RacingWheelConfig& config)
{
    enabled_             = config.override_cfg.enabled;
    steering_threshold_  = config.override_cfg.steering_threshold;
    throttle_threshold_  = config.override_cfg.throttle_threshold;
    brake_threshold_     = config.override_cfg.brake_threshold;
    auto_return_timeout_ = config.override_cfg.auto_return_timeout;
    button_override_     = config.override_cfg.button_override;
    mode_                = Mode::AUTO;
    idle_timer_          = 0.0;
}

void OverrideManager::Update(const InputFrame& input, double dt)
{
    if (!enabled_)
    {
        mode_ = Mode::MANUAL;
        return;
    }

    bool driver_active = false;

    if (input.pedal_steer)
    {
        const auto& ps = *input.pedal_steer;
        driver_active = std::abs(ps.steering) > steering_threshold_ ||
                        ps.throttle > throttle_threshold_ ||
                        ps.brake > brake_threshold_;

        if (button_override_ && (ps.buttons & 0x01))
        {
            driver_active = true;
        }
    }

#ifdef GT_ENABLE_OSI_MOTION_REQUEST
    if (input.motion_request)
    {
        // Any motion request from AD software is treated as active input
        driver_active = true;
    }
#endif

    if (driver_active)
    {
        mode_ = Mode::MANUAL;
        idle_timer_ = 0.0;
    }
    else if (mode_ == Mode::MANUAL)
    {
        if (auto_return_timeout_ > 0.0)
        {
            idle_timer_ += dt;
            if (idle_timer_ >= auto_return_timeout_)
            {
                mode_ = Mode::AUTO;
                idle_timer_ = 0.0;
            }
        }
        // If auto_return_timeout == 0, stay in MANUAL until RequestAutoMode()
    }
}

void OverrideManager::RequestAutoMode()
{
    mode_ = Mode::AUTO;
    idle_timer_ = 0.0;
}

} // namespace gt_esmini
