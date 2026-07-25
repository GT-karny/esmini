#pragma once

#include "gt_esmini/control/manualdrive/IFFBSink.hpp"  // FfbInterventionSample
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

    // feature:F7 (F7b) — stash the latest FFB torque-proxy sample. Called by
    // the controller BEFORE Update() each frame; consumed inside Update() only
    // when the servo is active and the ffb.target_track thresholds are crossed
    // for at least override_sustain_time seconds. Feeds the existing lateral
    // latch — never gates the release path (spike §2d).
    void UpdateFfbSample(const FfbInterventionSample& sample);

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

    // feature:F7 (F7b) — FFB torque-proxy latch (see UpdateFfbSample).
    FfbInterventionSample ffb_sample_               = {};
    double                ffb_force_threshold_      = 0.20;
    double                ffb_dev_threshold_        = 0.04;
    double                ffb_sustain_time_         = 0.10;
    double                ffb_sustain_accum_        = 0.0;
    // Rate-gate state: two gates + shared history validity flag. On the
    // FIRST active sample after ffb goes active there is no previous frame
    // to compare against, so any threshold check with rate=0 (bootstrap
    // fake) would spuriously permit accumulation while the servo is
    // actually mid-transient. The `history_valid_` flag suppresses the
    // detector entirely until the 2nd active sample, at which point BOTH
    // derivatives are meaningful. See Update() comment.
    //
    // Two gates:
    //   target_rate_gate  — |d(target)/dt|: AD actively steering
    //   position_error_rate_gate — |d(dev)/dt|: servo actively catching up
    // The detector requires BOTH rates below their gates AND thresholds
    // crossed to accumulate sustain. Real block = both rates ≈ 0 (target
    // static, dev static) AND thresholds crossed. Anything else = transient.
    double                ffb_target_rate_gate_       = 0.30;
    double                ffb_derror_rate_gate_       = 0.10;
    double                ffb_prev_target_norm_       = 0.0;
    double                ffb_prev_pos_error_         = 0.0;
    bool                  ffb_history_valid_          = false;
};

} // namespace gt_esmini
