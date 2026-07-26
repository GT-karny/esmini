#pragma once

#include "gt_esmini/control/manualdrive/IFFBSink.hpp"  // FfbInterventionSample
#include "gt_esmini/control/manualdrive/ManualDriveTypes.hpp"

#include <deque>

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

    // feature:F7 — stash the latest FFB servo sample. Called by the controller
    // BEFORE Update() each frame; consumed inside Update() only when the servo
    // is active, and only to drive the residual detector (the wheel's measured
    // position vs. the shadow plant's prediction of an unheld wheel) for at
    // least override_sustain_time seconds. Feeds the existing lateral latch —
    // never gates the release path (spike §2d): release is AUTO_RESUME only.
    void UpdateFfbSample(const FfbInterventionSample& sample);

    // feature:F7 — real-machine "why didn't it latch" diagnostics. Reflects the
    // residual detector's state as of the most recent Update() call; all
    // false/zero when the FFB sample is inactive. Exposed so telemetry
    // (VirtualDriverTelemetry) can show what the detector saw without
    // re-instrumenting the code — this observability gap is what made the
    // earlier real-machine failures so hard to diagnose.
    struct FfbLatchDiagnostics
    {
        // Single human-readable answer to "why isn't it latching right now?"
        // NONE means nothing is blocking (sustain is accumulating this frame,
        // or the domain is already latched MANUAL).
        enum class BlockReason
        {
            NONE,               // accumulating this frame, or already latched
            INACTIVE,           // FFB sample inactive / lateral domain not manual
            BOOTSTRAP,          // shadow not seeded yet (1st active sample)
            BELOW_RESIDUAL,     // |actual - shadow| <= residual_threshold
        };

        bool   sample_active          = false;  // ffb_sample_.active this frame
        bool   bootstrap_suppressed   = false;  // shadow not seeded yet (1st active sample)
        // --- Observational context (does NOT gate the latch; see
        //     ManualDriveConfig.ffb.target_track for why these thresholds
        //     are retained as diagnostics only) -------------------------
        bool   over_force             = false;  // |commanded_force| > force_threshold
        bool   over_dev               = false;  // |position_error| > dev_threshold
        bool   moving_target          = false;  // |d(target)/dt| > target_rate_gate — "AD is steering"
        bool   tracking_transient     = false;  // |d(position_error)/dt| > derror_rate_gate — "servo catching up"
        double target_rate            = 0.0;    // d(target_norm)/dt, axis-frac/s
        double derror_rate            = 0.0;    // d(position_error)/dt, axis-frac/s
        // --- The detector proper ------------------------------------------
        double actual_norm            = 0.0;    // target_norm - position_error (the real axis)
        double shadow_norm            = 0.0;    // where an UNHELD wheel would be, per the plant model
        double residual               = 0.0;    // |actual_norm - shadow_norm| — THE detection signal
        double residual_threshold     = 0.0;    // configured threshold, for context
        double effective_force        = 0.0;    // signed force driving the shadow this frame
        bool   shadow_moving          = false;  // shadow plant is in its kinetic (moving) regime
        double sustain_accum          = 0.0;    // seconds accumulated toward sustain_time (is it growing?)
        double sustain_time           = 0.0;    // configured sustain_time, for context
        BlockReason block_reason      = BlockReason::NONE;
    };
    const FfbLatchDiagnostics& GetFfbLatchDiagnostics() const { return ffb_diag_; }

    // Domain-level queries
    bool IsLateralManual() const { return lat_mode_ == Mode::MANUAL; }
    bool IsLongitudinalManual() const { return long_mode_ == Mode::MANUAL; }
    bool IsAnyManual() const { return lat_mode_ == Mode::MANUAL || long_mode_ == Mode::MANUAL; }
    bool IsEnabled() const { return enabled_; }

    // feature:F7 — AUTO_RESUME button rising edge: true on the frame the
    // driver PRESSED it, whether or not it changed anything.
    //
    // Exposed separately from JustTransitionedToAuto() because that one only
    // fires when the domain actually WAS manual. A driver who tries to take
    // over, fails to trigger a latch, and presses RESUME anyway leaves NO
    // trace in the transition stream — so a recorded session cannot be split
    // into attempts, and the one outcome worth measuring ("which attempt
    // failed to latch?") is unrecoverable. The button press is the driver's
    // own delimiter of an attempt, so it is what a session analysis must key
    // off. See scripts/ffb_spike/wheel_session_report.py.
    bool JustPressedResume() const { return resume_edge_; }

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
    bool   resume_edge_                 = false;  // this frame's rising edge (observability)

    // feature:F7 — FFB residual-based intervention latch (see UpdateFfbSample
    // and ManualDriveConfig.ffb.target_track for the full design rationale).
    FfbInterventionSample ffb_sample_               = {};
    double                ffb_sustain_time_         = 0.10;
    double                ffb_sustain_accum_        = 0.0;
    // Observational-only thresholds/gates. Reported through
    // FfbLatchDiagnostics; they do NOT gate the latch.
    double                ffb_force_threshold_        = 0.20;
    double                ffb_dev_threshold_          = 0.04;
    double                ffb_target_rate_gate_       = 0.30;
    double                ffb_derror_rate_gate_       = 0.10;
    double                ffb_prev_target_norm_       = 0.0;
    double                ffb_prev_pos_error_         = 0.0;
    bool                  ffb_history_valid_          = false;

    // --- Shadow model: "where would an UNHELD wheel be right now?" ---------
    // A 1-D stick-slip plant integrated every frame from the EFFECTIVE force
    // (FfbInterventionSample::effective_force_signed — NOT the feedback-only
    // force; see that field's comment). Constants are the real-G29 measured
    // friction/velocity characteristic. The detector fires on the distance
    // between this prediction and the measured axis, which is why it works
    // regardless of which direction the driver pushes.
    double                ffb_shadow_norm_            = 0.0;
    bool                  ffb_shadow_moving_          = false;  // stick-slip regime
    bool                  ffb_shadow_valid_           = false;  // seeded from the 1st active sample
    double                ffb_residual_threshold_     = 0.08;
    double                ffb_residual_reanchor_tau_  = 0.30;
    double                ffb_shadow_breakaway_       = 0.21;
    double                ffb_shadow_breakaway_left_  = 0.170;   // force > 0 (pushes wheel left)
    double                ffb_shadow_breakaway_right_ = 0.190;   // force < 0 (pushes wheel right)
    double                ffb_shadow_motion_eps_      = 0.01;
    // Measured axis at the moment the shadow last came to rest. The
    // "demonstrably moving" test is a DISPLACEMENT from this anchor, not a
    // per-frame rate, so bounded column jitter can never satisfy it.
    double                ffb_shadow_rest_anchor_     = 0.0;
    double                ffb_shadow_kinetic_         = 0.16;
    double                ffb_shadow_force_to_vel_    = 3.35;
    double                ffb_shadow_v_max_           = 1.0;
    // Mechanical inertia: the measured force->velocity curve is a
    // STEADY-STATE map, so the shadow reached it instantly while a real
    // wheel cannot. See the note at the integration site.
    double                ffb_shadow_velocity_tau_    = 0.10;
    double                ffb_shadow_vel_             = 0.0;
    // Transport delay. 0 = disabled until measured.
    double                ffb_shadow_dead_time_       = 0.0;
    // Onset grace (see ManualDriveConfig): re-sync instead of banking residual
    // while the shadow's and the measurement's motion states disagree, but only
    // for as long as the transition itself could plausibly last.
    double                ffb_shadow_onset_grace_     = 0.0;
    double                ffb_shadow_motion_rate_eps_ = 0.02;
    double                ffb_disagree_elapsed_       = 0.0;
    bool                  ffb_disagree_active_        = false;
    struct ForceSample { double force; double dt; };
    std::deque<ForceSample> ffb_force_history_;
    FfbLatchDiagnostics   ffb_diag_;
};

} // namespace gt_esmini
