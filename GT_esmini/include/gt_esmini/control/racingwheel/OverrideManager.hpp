#pragma once

#include "gt_esmini/control/racingwheel/RacingWheelTypes.hpp"

namespace gt_esmini
{

struct RacingWheelConfig;

class OverrideManager
{
public:
    enum class Mode
    {
        AUTO,
        MANUAL
    };

    void Configure(const RacingWheelConfig& config);
    void Update(const InputFrame& input, double dt);
    void RequestAutoMode();

    Mode GetMode() const { return mode_; }
    bool IsManualMode() const { return mode_ == Mode::MANUAL; }
    bool IsEnabled() const { return enabled_; }

private:
    bool   enabled_             = true;
    double steering_threshold_  = 0.05;
    double throttle_threshold_  = 0.1;
    double brake_threshold_     = 0.1;
    double auto_return_timeout_ = 0.0;
    bool   button_override_     = true;

    Mode   mode_       = Mode::AUTO;
    double idle_timer_ = 0.0;
};

} // namespace gt_esmini
