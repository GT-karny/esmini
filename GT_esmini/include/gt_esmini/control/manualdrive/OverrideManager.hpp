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

        // feature:F7 — re-anchor instrument (test_results/f7_reanchor_instrument_spec.md).
        // Purely observational: mirrors how often and how much the detector's
        // OWN shadow gets forcibly re-synced to the measured axis (which erases
        // residual outside of the normal plant integration), plus a
        // never-re-anchored "free-running" shadow that shows what the residual
        // would have been if none of that erasure had happened. None of this
        // feeds back into block_reason/residual/sustain_accum above.
        enum class ReanchorSource
        {
            NONE,            // no re-anchor event this frame
            SEED,            // S1 — shadow (re-)seeded with no preceding RESUME/inactive re-arm
            ONSET_GRACE,     // S3 — onset-grace re-sync (see OverrideManager.cpp :397 comment)
            DRIFT,           // S4 — slow drift correction toward the measured axis
            RESUME,          // S1 fired because AUTO_RESUME (S5) invalidated the shadow
            INACTIVE_REARM,  // S1 fired because the FFB sample went inactive (S6) invalidated the shadow
            SERVO_TRACKING,  // S7 — the wheel is moving WITH the AD target under a force too
                             // small to move the shadow, and the tracking error is not growing.
                             // Servo-carried motion, not a hand (OverrideManager.cpp).
        };

        // Both counters and both accumulators only count an event that
        // actually MOVED the shadow (|delta| above floating-point noise),
        // with one exception: S1 (seed) always counts, delta or not — an
        // arm/re-arm is itself the thing worth counting, not just the
        // displacement it happens to produce. Without this, S4 (which
        // re-evaluates every frame residual<=threshold) would inflate
        // reanchor_soft_count into a near-frame-count rather than "how many
        // times did drift correction actually nudge the shadow".
        int    reanchor_hard_count           = 0;  // cumulative S1 (always) + S3 (only if it moved the shadow)
        int    reanchor_soft_count           = 0;  // cumulative S4 firings that actually moved the shadow
        double reanchor_delta                = 0.0;  // this frame's shadow displacement from a re-anchor event; 0 if none
        // Split per spec §1-1 / §2 (revised): §3-1's question — "is S3 or S4
        // responsible for the 1-frame erasure?" — cannot be answered if the
        // two are summed into one accumulator. hard = S1+S3 (what §3-1
        // measures), soft = S4 (drift leak). Never add to the wrong one.
        double reanchor_hard_delta_abs_accum = 0.0;  // cumulative |delta| from S1+S3 (hard) re-anchors
        double reanchor_soft_delta_abs_accum = 0.0;  // cumulative |delta| from S4 (soft) drift correction
        ReanchorSource reanchor_source  = ReanchorSource::NONE;  // which path fired this frame (hard wins if both)
        double free_shadow_norm         = 0.0;  // shadow integrated (S2 only) WITHOUT S3/S4 re-anchoring
        double free_residual            = 0.0;  // |actual_norm - free_shadow_norm| — the "what if nothing had
                                                 // been erased" residual.
        // NOT guaranteed to be >= residual on any given frame (an earlier
        // draft of this spec claimed it was; retracted — see
        // OverrideManager.cpp for the counter-example: S3 zeroes only the
        // real shadow's velocity, so the two shadows can briefly integrate
        // from mismatched hysteresis state and free can transiently read
        // closer to actual than the real, erased shadow). free_below_real_count
        // OBSERVES how often that happens instead of asserting it can't. The
        // comparison that actually matters is walk-level max(free_residual)
        // vs max(residual) over the whole run, not per-frame ordering.
        int    free_below_real_count    = 0;    // cumulative frames where free_residual < residual, since run start
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

    // feature:F7 — startup axis reference for the direct-axis lateral check.
    // A physical wheel keeps the angle the PREVIOUS session left it at, so the
    // axis level on frame 1 is not evidence of a driver. See the Update() site
    // for the measurement this comes from. While the reference is active the
    // direct-axis test measures CHANGE from it instead of absolute position;
    // it deactivates permanently the first time the wheel is seen inside the
    // neutral band, after which behavior is bit-identical to the plain test.
    bool   startup_axis_seen_       = false;
    bool   startup_axis_ref_active_ = false;
    double startup_axis_ref_        = 0.0;

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

    // --- feature:F7 re-anchor instrument (observational only) --------------
    // See test_results/f7_reanchor_instrument_spec.md. These counters live
    // OUTSIDE ffb_diag_ because they are cumulative "since run start" values
    // and must survive the `ffb_diag_ = {}` reset in the inactive branch of
    // Update() (OverrideManager.cpp) — they are copied into ffb_diag_ each
    // frame instead.
    int    reanchor_hard_count_           = 0;
    int    reanchor_soft_count_           = 0;
    double reanchor_hard_delta_abs_accum_ = 0.0;  // S1+S3 only — see FfbLatchDiagnostics field comment
    double reanchor_soft_delta_abs_accum_ = 0.0;  // S4 only
    // Observed (not asserted — see FfbLatchDiagnostics::free_residual comment)
    // count of frames where the never-erased free shadow's residual reads
    // BELOW the real, erased detector's residual.
    int    free_below_real_count_    = 0;
    // Which re-arm path most recently invalidated the shadow (S5 RESUME /
    // S6 inactive), consumed and tagged onto the NEXT S1 seed event (S1
    // itself carries no information about why it is seeding). Defaults to
    // SEED so the very first seed of a run — with no preceding RESUME or
    // inactive re-arm — is tagged as a plain genesis seed.
    FfbLatchDiagnostics::ReanchorSource reanchor_pending_source_ =
        FfbLatchDiagnostics::ReanchorSource::SEED;

    // Free-running shadow: the SAME 1-D stick-slip plant as ffb_shadow_*
    // above (S2 integration only), but as a fully separate instance that is
    // NEVER touched by onset-grace (S3) or drift (S4) re-anchoring — only by
    // S1 (seed) / S5 / S6 (re-arm via invalidation), kept in lockstep with
    // ffb_shadow_valid_'s arm/re-arm cycle. This is what free_residual is
    // computed from. Deliberately not merged with ffb_shadow_* state: see
    // spec §3 for why sharing would mix the two shadows' behavior.
    double free_shadow_norm_        = 0.0;
    bool   free_shadow_moving_      = false;
    bool   free_shadow_valid_       = false;
    double free_shadow_vel_         = 0.0;
    double free_shadow_rest_anchor_ = 0.0;

    // S2-only plant integration for the free-running shadow (mirrors the
    // ffb_shadow_* integration block in Update() verbatim, on free_shadow_*
    // state). Defined in OverrideManager.cpp.
    void UpdateFreeShadowPlant(double f, double dt, double actual_norm);
};

} // namespace gt_esmini
