#include "gt_esmini/control/manualdrive/OverrideManager.hpp"
#include "gt_esmini/control/manualdrive/ManualDriveConfig.hpp"
#include "gt_esmini/control/common/VehicleCommand.hpp"

#include <algorithm>
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
    just_transitioned_to_manual_ = false;
    just_transitioned_to_auto_   = false;
    prev_resume_pressed_         = false;

    // feature:F7 — FFB residual detector. Independent of the
    // steering_threshold_ used for the direct pedal_steer.steering path.
    ffb_sustain_time_          = config.ffb.target_track.override_sustain_time;
    // Observational-only (see ManualDriveConfig / FfbLatchDiagnostics).
    ffb_force_threshold_       = config.ffb.target_track.override_steer_force_threshold;
    ffb_dev_threshold_         = config.ffb.target_track.override_steer_dev_threshold;
    ffb_target_rate_gate_      = config.ffb.target_track.override_target_rate_gate;
    ffb_derror_rate_gate_      = config.ffb.target_track.override_position_error_rate_gate;
    // Residual detector + shadow plant.
    ffb_residual_threshold_    = config.ffb.target_track.override_residual_threshold;
    ffb_residual_reanchor_tau_ = config.ffb.target_track.override_residual_reanchor_tau;
    ffb_shadow_breakaway_      = config.ffb.target_track.override_shadow_breakaway;
    ffb_shadow_breakaway_left_ = config.ffb.target_track.override_shadow_breakaway_left;
    ffb_shadow_breakaway_right_= config.ffb.target_track.override_shadow_breakaway_right;
    ffb_shadow_motion_eps_     = config.ffb.target_track.override_shadow_motion_epsilon;
    ffb_shadow_kinetic_        = config.ffb.target_track.override_shadow_kinetic;
    ffb_shadow_force_to_vel_   = config.ffb.target_track.override_shadow_force_to_velocity;
    ffb_shadow_v_max_          = config.ffb.target_track.override_shadow_v_max;
    ffb_sustain_accum_         = 0.0;
    ffb_prev_target_norm_      = 0.0;
    ffb_prev_pos_error_        = 0.0;
    ffb_history_valid_         = false;
    ffb_shadow_norm_           = 0.0;
    ffb_shadow_moving_         = false;
    ffb_shadow_valid_          = false;
    ffb_sample_                = {};
    ffb_diag_                  = {};
}

void OverrideManager::UpdateFfbSample(const FfbInterventionSample& sample)
{
    ffb_sample_ = sample;
}

void OverrideManager::Update(const InputFrame& input, double dt)
{
    just_transitioned_to_manual_ = false;
    just_transitioned_to_auto_   = false;

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

    const bool was_any_manual = IsAnyManual();

    // feature:F7 — AUTO_RESUME rising-edge detection.
    // A rising edge (this frame pressed, previous frame not) requests a hard
    // return to AUTO on both configured-manual domains, and suppresses the
    // threshold-based intervention checks for THIS frame so that a still-held
    // wheel/pedal doesn't instantly re-latch on the same frame. The next frame
    // (RESUME still held or released) is evaluated normally, so leaving the
    // wheel/pedal above threshold on the following frame will re-latch — the
    // real-world "driver still holding the wheel" case.
    const uint32_t buttons = input.pedal_steer ? input.pedal_steer->buttons : 0u;
    const bool resume_pressed = (buttons & ButtonBits::AUTO_RESUME) != 0;
    const bool resume_edge    = resume_pressed && !prev_resume_pressed_;
    prev_resume_pressed_ = resume_pressed;

    if (resume_edge)
    {
        if (lat_configured_manual_)  lat_mode_ = Mode::AUTO;
        if (long_configured_manual_) long_mode_ = Mode::AUTO;
        idle_timer_ = 0.0;
        // Also reset the FFB sustain accumulator + history so a still-
        // sustained push after RESUME gets a fresh window (would otherwise
        // re-latch on the very next frame). Matches "no, back to AUTO"
        // driver intent.
        //
        // The shadow is invalidated too, so it re-seeds from the measured
        // axis on the next active sample instead of carrying the residual
        // that caused the latch. Without this, a driver who RESUMEs while
        // still holding the wheel would re-latch instantly on the stale
        // residual, and — more importantly for the multi-cycle case — the
        // shadow would still be sitting wherever the pre-RESUME servo push
        // had driven it, so the SECOND intervention would be measured
        // against a meaningless reference.
        ffb_sustain_accum_ = 0.0;
        ffb_history_valid_ = false;
        ffb_shadow_valid_  = false;
        ffb_shadow_moving_ = false;
        if (was_any_manual)
            just_transitioned_to_auto_ = true;
        return;  // suppress same-frame intervention re-latch
    }

    bool lat_active  = false;
    bool long_active = false;

    if (input.pedal_steer)
    {
        const auto& ps = *input.pedal_steer;

        if (lat_configured_manual_)
        {
            // feature:F7 (F7b) closed-loop feedback protection.
            // While the target-track servo is active, the physical wheel is
            // being DRIVEN by the servo (SDLFFBSink CONSTANT force). The next
            // frame's SDL_JoystickGetAxis reads back the servo's own motion —
            // if we ran the direct-axis threshold check here, |ps.steering|
            // would cross steering_threshold_ purely because the servo moved
            // the wheel, and the manager would latch MANUAL on frame 2, which
            // would flip target_active_ off (via !lat_manual in
            // ControllerVirtualDriver::Step step 5a) and kill the servo. The
            // servo would then re-arm next frame, re-latch, re-die — but the
            // first latch already stuck (one-way latch), so from the user's
            // perspective the servo appears to never engage and any push-back
            // never seems to "fire" the override (it already fired silently).
            //
            // The correct detector under an active servo is the TORQUE PROXY
            // block below: it distinguishes "wheel where AD wants it"
            // (position_error≈0, no push) from "driver fighting the servo"
            // (position_error and commanded_force both grow). Direct-axis
            // path is preserved when the servo is off (target_track disabled,
            // stub/network input, ManualDrive-only run).
            if (!ffb_sample_.active)
            {
                lat_active = std::abs(ps.steering) > steering_threshold_;
            }
        }

        if (long_configured_manual_)
        {
            long_active = ps.throttle > throttle_threshold_ ||
                          ps.brake > brake_threshold_;
        }

        if (button_override_ && (ps.buttons & ButtonBits::OVERRIDE))
        {
            if (lat_configured_manual_)  lat_active = true;
            if (long_configured_manual_) long_active = true;
        }
    }

    // feature:F7 — FFB RESIDUAL intervention path. Fires the lateral latch
    // after the measured wheel has diverged from the shadow model's
    // prediction by more than residual_threshold for at least sustain_time.
    // Only meaningful for the LATERAL domain (steering); the pedal path stays
    // exclusive to the direct throttle/brake threshold check above.
    //
    // THE IDEA. Every frame we know the force actually delivered to the wheel
    // (FfbInterventionSample::effective_force_signed). The wheel's response to
    // a force is a MEASURED property of the device (real G29,
    // scripts/ffb_spike/CHARACTERIZATION.md §2/§3): a hard deadzone below
    // breakaway, then v ≈ 3.35·(|f| − 0.16) axis-frac/s saturating at ~1.0/s,
    // with a narrow static/kinetic hysteresis band. Integrating that plant
    // gives the SHADOW: where the wheel would be right now if nobody were
    // touching it. The distance between the real axis and the shadow is
    // physical evidence of an external hand.
    //
    // WHY THIS REPLACED THE DIRECTION-BASED DETECTOR. The old test asked
    // whether the wheel was sign-opposed to, or past, the AD target. But an
    // override is not necessarily against AD's direction: the most natural
    // human reaction to an over-aggressive AD steering command is to HOLD THE
    // WHEEL SHORT OF IT — same sign, |actual| < |target| — which is exactly
    // the region the old test defined as "obedient wheel" and could never
    // fire in. The residual has no direction assumption at all, and is
    // likewise invariant to how fast AD is moving the target, so it needs
    // none of the rate gates that used to black out detection during the
    // post-RESUME envelope ramp.
    //
    // The old real-machine false-positive classes are covered PHYSICALLY
    // rather than by gates:
    //   - mechanically stuck wheel / terminal state (b6dc58f0, and the
    //     2026-07-26 hands-off measurement: effective force 0.166-0.180
    //     pushing RIGHT, tracking error 0.0112-0.0120, displacement exactly
    //     zero over 7.5 s): that force is below the right-hand breakaway band
    //     (0.190-0.210), so the shadow does not move either → residual ≡ 0.
    //   - startup / servo-catch-up transients (549e5823, f8a5ce56): the
    //     shadow is driven by the same force that is accelerating the real
    //     wheel, so both move together → residual ≈ 0.
    //   - moving target during curves / lane changes (a43e4c67): the shadow
    //     chases the same force the servo emits → residual ≈ 0.
    //
    // Bootstrap: the very first active sample seeds the shadow FROM the
    // measured axis (residual ≡ 0 by construction) and cannot latch. Rate
    // history needs two samples for the same reason; the rates are now purely
    // observational, but they are still bootstrap-suppressed so the telemetry
    // never shows a fabricated derivative.
    if (lat_configured_manual_ && ffb_sample_.active)
    {
        const bool suppress = !ffb_history_valid_ || dt <= 1e-6;

        // The sink records position_error = target_norm - actual_norm exactly,
        // so the measured axis is recoverable from the sample.
        const double actual_norm = ffb_sample_.target_norm - ffb_sample_.position_error;

        double target_rate = 0.0;
        double derror_rate = 0.0;
        if (!suppress)
        {
            target_rate = (ffb_sample_.target_norm    - ffb_prev_target_norm_) / dt;
            derror_rate = (ffb_sample_.position_error - ffb_prev_pos_error_)   / dt;
        }
        // Store current sample as prev for the next frame (always, so the
        // 2nd active frame has valid history even if the 1st was suppressed).
        ffb_prev_target_norm_ = ffb_sample_.target_norm;
        ffb_prev_pos_error_   = ffb_sample_.position_error;
        ffb_history_valid_    = true;

        // Observational context only — none of these gate the latch.
        const bool moving_target      = std::abs(target_rate) > ffb_target_rate_gate_;
        const bool tracking_transient = std::abs(derror_rate) > ffb_derror_rate_gate_;
        const bool over_force         = std::abs(ffb_sample_.commanded_force) > ffb_force_threshold_;
        const bool over_dev           = std::abs(ffb_sample_.position_error)  > ffb_dev_threshold_;

        // --- Shadow plant integration -----------------------------------
        // Seed on the first active sample: the shadow starts wherever the
        // real wheel is, so the detector can never fire on a stale reference.
        if (!ffb_shadow_valid_)
        {
            ffb_shadow_norm_        = actual_norm;
            ffb_shadow_rest_anchor_ = actual_norm;
            ffb_shadow_moving_      = false;
            ffb_shadow_valid_       = true;
        }

        const double f = ffb_sample_.effective_force_signed;
        if (!suppress)
        {
            // Stick-slip: a wheel at rest needs `breakaway` to start; once
            // moving it keeps moving until the force drops below `kinetic`.
            // The measured 0.02-0.03 gap between the two is the hysteresis
            // that stops a force hovering at the threshold from chattering.
            //
            // ONSET IS GATED BY BREAKAWAY, NEVER BY `kinetic` — see
            // ManualDriveConfig for the measurement and for what using 0.16
            // here would cost. Breakaway is a measured, DIRECTION-ASYMMETRIC
            // band, so it is applied in two arms: the top of the band starts
            // the shadow unconditionally (any wheel is moving by then), while
            // the per-direction band bottom starts it only when the real axis
            // is demonstrably moving — that observation is what settles where
            // in the band this particular device sits.
            if (!ffb_shadow_moving_)
            {
                // Positive force pushes the wheel LEFT, negative RIGHT
                // (FfbTargetServo.hpp). The right-hand band bottom is the
                // higher of the two, which is what keeps the measured
                // hands-off right-push stretch (|f| <= 0.180) below onset.
                const double band_bottom = (f >= 0.0) ? ffb_shadow_breakaway_left_
                                                      : ffb_shadow_breakaway_right_;
                const bool observed_moving =
                    std::abs(actual_norm - ffb_shadow_rest_anchor_) > ffb_shadow_motion_eps_;
                if (std::abs(f) >= ffb_shadow_breakaway_ ||
                    (std::abs(f) >= band_bottom && observed_moving))
                {
                    ffb_shadow_moving_ = true;
                }
            }
            double shadow_vel = 0.0;
            if (ffb_shadow_moving_)
            {
                const double excess = std::abs(f) - ffb_shadow_kinetic_;
                if (excess <= 0.0)
                {
                    // Kinetic friction absorbed it: the shadow comes to rest,
                    // and the measured axis becomes the new at-rest anchor
                    // for the observed-motion test above.
                    ffb_shadow_moving_      = false;
                    ffb_shadow_rest_anchor_ = actual_norm;
                }
                else
                {
                    const double speed = std::min(ffb_shadow_force_to_vel_ * excess,
                                                  ffb_shadow_v_max_);
                    // Sign: positive force pushes the wheel LEFT, which is the
                    // NEGATIVE axis direction (FfbTargetServo.hpp / spike §1f).
                    shadow_vel = (f >= 0.0) ? -speed : speed;
                }
            }
            ffb_shadow_norm_ = std::clamp(ffb_shadow_norm_ + shadow_vel * dt, -1.0, 1.0);
        }

        const double residual = std::abs(actual_norm - ffb_shadow_norm_);

        // --- Latch decision ----------------------------------------------
        if (suppress || residual <= ffb_residual_threshold_)
        {
            ffb_sustain_accum_ = 0.0;
        }
        else
        {
            ffb_sustain_accum_ += dt;
            if (ffb_sustain_accum_ >= ffb_sustain_time_)
            {
                lat_active = true;
            }
        }

        // --- Drift control (mandatory: the shadow is an integrator) -------
        // Model error accumulates without bound, so a long drive would
        // eventually false-latch on drift alone. While there is NO evidence
        // of a driver (residual under threshold) and we are not latched, pull
        // the shadow back toward the measured axis with a first-order time
        // constant. This bounds standing drift at the threshold while leaving
        // a real intervention — which diverges an order of magnitude faster
        // than the leak can pull back — free to cross it.
        //
        // Not applied once latched: the shadow is re-seeded at RESUME
        // instead (see the resume_edge branch), so the next cycle starts from
        // a clean reference rather than one dragged around during MANUAL.
        if (!suppress && !lat_active && lat_mode_ != Mode::MANUAL &&
            residual <= ffb_residual_threshold_)
        {
            const double alpha = (ffb_residual_reanchor_tau_ > 1e-6)
                                     ? (1.0 - std::exp(-dt / ffb_residual_reanchor_tau_))
                                     : 1.0;
            ffb_shadow_norm_ += alpha * (actual_norm - ffb_shadow_norm_);
        }

        // feature:F7 — real-machine "why didn't it latch" diagnostics.
        // Purely observational: mirrors the decision computed above into
        // ffb_diag_ for telemetry (GetFfbLatchDiagnostics() /
        // VirtualDriverTelemetry.ffb_gate_*). Does not feed back into it.
        using BlockReason = FfbLatchDiagnostics::BlockReason;
        BlockReason block_reason = BlockReason::NONE;
        if (suppress)
        {
            block_reason = BlockReason::BOOTSTRAP;
        }
        else if (residual <= ffb_residual_threshold_)
        {
            block_reason = BlockReason::BELOW_RESIDUAL;
        }
        // else: NONE (accumulating this frame, or lat_active already set)

        ffb_diag_.sample_active        = true;
        ffb_diag_.bootstrap_suppressed = suppress;
        ffb_diag_.over_force           = over_force;
        ffb_diag_.over_dev             = over_dev;
        ffb_diag_.moving_target        = moving_target;
        ffb_diag_.tracking_transient   = tracking_transient;
        ffb_diag_.target_rate          = target_rate;
        ffb_diag_.derror_rate          = derror_rate;
        ffb_diag_.actual_norm          = actual_norm;
        ffb_diag_.shadow_norm          = ffb_shadow_norm_;
        ffb_diag_.residual             = residual;
        ffb_diag_.residual_threshold   = ffb_residual_threshold_;
        ffb_diag_.effective_force      = f;
        ffb_diag_.shadow_moving        = ffb_shadow_moving_;
        ffb_diag_.sustain_accum        = ffb_sustain_accum_;
        ffb_diag_.sustain_time         = ffb_sustain_time_;
        ffb_diag_.block_reason         = block_reason;
    }
    else
    {
        // Sample missing or servo off: reset accumulator + history + shadow so
        // a stale burst can never be revived on the next Configure/enable
        // transition. All three re-arm at the next active sample.
        ffb_sustain_accum_ = 0.0;
        ffb_history_valid_ = false;
        ffb_shadow_valid_  = false;
        ffb_shadow_moving_ = false;
        ffb_diag_          = {};
        ffb_diag_.block_reason = FfbLatchDiagnostics::BlockReason::INACTIVE;
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

    // Detect AUTO→MANUAL transition
    if (!was_any_manual && IsAnyManual())
        just_transitioned_to_manual_ = true;

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
                if (was_any_manual)
                    just_transitioned_to_auto_ = true;
            }
        }
    }
}

void OverrideManager::RequestAutoMode()
{
    const bool was_any_manual = IsAnyManual();
    if (lat_configured_manual_)  lat_mode_ = Mode::AUTO;
    if (long_configured_manual_) long_mode_ = Mode::AUTO;
    idle_timer_ = 0.0;
    if (was_any_manual)
        just_transitioned_to_auto_ = true;
}

} // namespace gt_esmini
