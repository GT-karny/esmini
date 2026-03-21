#include "gt_esmini/control/manualdrive/OverrideManager.hpp"
#include "gt_esmini/control/manualdrive/ManualDriveConfig.hpp"

#include <cmath>

namespace gt_esmini
{

void OverrideManager::Configure(const ManualDriveConfig& config)
{
    enabled_             = config.override_cfg.enabled;
    steering_threshold_  = config.override_cfg.steering_threshold;
    throttle_threshold_  = config.override_cfg.throttle_threshold;
    brake_threshold_     = config.override_cfg.brake_threshold;
    auto_return_timeout_ = config.override_cfg.auto_return_timeout;
    button_override_     = config.override_cfg.button_override;

    // Domain configuration
    lat_configured_manual_  = (config.domain.lateral == "manual");
    long_configured_manual_ = (config.domain.longitudinal == "manual");

    lat_mode_  = Mode::AUTO;
    long_mode_ = Mode::AUTO;
    idle_timer_ = 0.0;
}

void OverrideManager::Update(const InputFrame& input, double dt)
{
    // Domains configured as "scenario" are always AUTO
    if (!lat_configured_manual_)  lat_mode_ = Mode::AUTO;
    if (!long_configured_manual_) long_mode_ = Mode::AUTO;

    if (!enabled_)
    {
        // Override disabled: configured-manual domains are always MANUAL
        if (lat_configured_manual_)  lat_mode_ = Mode::MANUAL;
        if (long_configured_manual_) long_mode_ = Mode::MANUAL;
        return;
    }

    bool lat_active = false;
    bool long_active = false;

    if (input.pedal_steer)
    {
        const auto& ps = *input.pedal_steer;

        if (lat_configured_manual_)
        {
            lat_active = std::abs(ps.steering) > steering_threshold_;
        }

        if (long_configured_manual_)
        {
            long_active = ps.throttle > throttle_threshold_ ||
                          ps.brake > brake_threshold_;
        }

        if (button_override_ && (ps.buttons & 0x01))
        {
            if (lat_configured_manual_)  lat_active = true;
            if (long_configured_manual_) long_active = true;
        }
    }

#ifdef GT_ENABLE_OSI_MOTION_REQUEST
    if (input.motion_request)
    {
        if (lat_configured_manual_)  lat_active = true;
        if (long_configured_manual_) long_active = true;
    }
#endif

    if (lat_active)  lat_mode_ = Mode::MANUAL;
    if (long_active) long_mode_ = Mode::MANUAL;

    // Idle timer for auto-return
    bool any_active = lat_active || long_active;
    if (any_active)
    {
        idle_timer_ = 0.0;
    }
    else if (IsAnyManual())
    {
        if (auto_return_timeout_ > 0.0)
        {
            idle_timer_ += dt;
            if (idle_timer_ >= auto_return_timeout_)
            {
                if (lat_configured_manual_)  lat_mode_ = Mode::AUTO;
                if (long_configured_manual_) long_mode_ = Mode::AUTO;
                idle_timer_ = 0.0;
            }
        }
    }
}

void OverrideManager::RequestAutoMode()
{
    if (lat_configured_manual_)  lat_mode_ = Mode::AUTO;
    if (long_configured_manual_) long_mode_ = Mode::AUTO;
    idle_timer_ = 0.0;
}

} // namespace gt_esmini
