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

    // feature:F7 (F7b, follow-up post-93b2c6c4) — real-machine "why didn't it
    // latch" diagnostics. Reflects the torque-proxy gate state as of the most
    // recent Update() call; all false/zero when the FFB sample is inactive.
    // Exposed so telemetry (VirtualDriverTelemetry) can show which gate is
    // blocking the sustain accumulator without re-instrumenting the code —
    // this observability gap is what made the envelope-ramp blackout hard to
    // diagnose from a real-machine session in the first place.
    struct FfbLatchDiagnostics
    {
        // Single human-readable answer to "why isn't it latching right now?"
        // Priority order matches the actual gating logic in Update() — see
        // there for how each value is derived. NONE means nothing is blocking
        // (sustain is accumulating this frame, or the domain is already
        // latched MANUAL).
        enum class BlockReason
        {
            NONE,               // accumulating this frame, or already latched
            INACTIVE,           // FFB sample inactive / lateral domain not manual
            BOOTSTRAP,          // no rate history yet (1st active sample)
            BELOW_THRESHOLD,    // neither over_force nor over_dev crossed
            // MOVING_TARGET / TRACKING_TRANSIENT: position signature shows
            // standing opposition but is rate-gated (velocity signature not
            // opposing this exact frame either). The clock is HELD (not
            // reset) whenever wheel_engaged_position is true, so this reason
            // can mean either "paused, holding prior progress" or "reset from
            // zero" depending on whether sustain_accum > 0 — check that field.
            MOVING_TARGET,
            TRACKING_TRANSIENT,
            WHEEL_NOT_ENGAGED,  // rates settled but neither opposition signature fired
        };

        bool   sample_active          = false;  // ffb_sample_.active this frame
        bool   bootstrap_suppressed   = false;  // no rate history yet (1st active sample)
        bool   over_force             = false;  // |commanded_force| > force_threshold
        bool   over_dev               = false;  // |position_error| > dev_threshold
        bool   moving_target          = false;  // |d(target)/dt| > target_rate_gate
        bool   tracking_transient     = false;  // |d(position_error)/dt| > derror_rate_gate
        bool   sign_opposition        = false;  // actual/target opposite sign (position signature, arm 1)
        bool   magnitude_opposition   = false;  // |actual| > |target| + eps (position signature, arm 2)
        bool   wheel_engaged_position = false;  // sign_opposition || magnitude_opposition
        bool   wheel_engaged_velocity = false;  // force-vs-velocity opposition (rate-invariant)
        bool   driver_opposing        = false;  // combined signal actually gating accumulation
        double target_rate            = 0.0;    // d(target_norm)/dt, axis-frac/s
        double derror_rate            = 0.0;    // d(position_error)/dt, axis-frac/s
        double actual_rate            = 0.0;    // d(actual_norm)/dt, axis-frac/s
        double actual_norm            = 0.0;    // target_norm - position_error
        double sustain_accum          = 0.0;    // seconds accumulated toward sustain_time (most important field: is it growing?)
        double sustain_time           = 0.0;    // configured sustain_time, for context
        BlockReason block_reason      = BlockReason::NONE;
    };
    const FfbLatchDiagnostics& GetFfbLatchDiagnostics() const { return ffb_diag_; }

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
    // feature:F7 (F7b, follow-up post-f723fa90) — wheel-opposing-target gate.
    // See ManualDriveConfig.ffb.target_track.override_wheel_over_target_epsilon.
    double                ffb_wheel_over_target_eps_  = 0.05;
    // feature:F7 (F7b, follow-up post-93b2c6c4) — velocity-opposition gate.
    // See ManualDriveConfig.ffb.target_track.override_opposition_velocity_gate.
    double                ffb_opposition_vel_gate_    = 0.30;
    FfbLatchDiagnostics   ffb_diag_;
};

} // namespace gt_esmini
