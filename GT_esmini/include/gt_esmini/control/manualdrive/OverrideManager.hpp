#pragma once

#include "gt_esmini/control/manualdrive/ManualDriveTypes.hpp"

namespace gt_esmini
{

struct ManualDriveConfig;

class OverrideManager
{
public:
    enum class Mode
    {
        AUTO,
        MANUAL
    };

    void Configure(const ManualDriveConfig& config);
    void Update(const InputFrame& input, double dt);
    void RequestAutoMode();

    // Domain-level queries
    bool IsLateralManual() const { return lat_mode_ == Mode::MANUAL; }
    bool IsLongitudinalManual() const { return long_mode_ == Mode::MANUAL; }
    bool IsAnyManual() const { return lat_mode_ == Mode::MANUAL || long_mode_ == Mode::MANUAL; }
    bool IsEnabled() const { return enabled_; }

    // Transition detection: true only on the frame the AUTO→MANUAL or
    // MANUAL→AUTO transition occurred. feature:F7 telemetry consumer.
    bool JustTransitionedToManual() const { return just_transitioned_to_manual_; }
    bool JustTransitionedToAuto()   const { return just_transitioned_to_auto_; }

    // Legacy compatibility
    bool IsManualMode() const { return IsAnyManual(); }

private:
    bool   enabled_             = true;
    double steering_threshold_  = 0.05;
    double throttle_threshold_  = 0.1;
    double brake_threshold_     = 0.1;
    double auto_return_timeout_ = 0.0;
    bool   button_override_     = true;

    // Domain configuration: which domains can be manually controlled
    bool lat_configured_manual_  = true;
    bool long_configured_manual_ = true;

    // Runtime state per domain
    Mode   lat_mode_   = Mode::AUTO;
    Mode   long_mode_  = Mode::AUTO;
    double idle_timer_ = 0.0;
    bool   just_transitioned_to_manual_ = false;
    bool   just_transitioned_to_auto_   = false;
    bool   prev_resume_pressed_         = false;  // feature:F7 rising-edge detector
};

} // namespace gt_esmini
