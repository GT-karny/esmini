#include "gt_esmini/control/manualdrive/OverrideManager.hpp"
#include "gt_esmini/control/manualdrive/ManualDriveConfig.hpp"
#include "gt_esmini/control/common/VehicleCommand.hpp"

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

    // feature:F7 (F7b) — FFB torque-proxy thresholds. Independent of the
    // steering_threshold_ used for the direct pedal_steer.steering path.
    ffb_force_threshold_       = config.ffb.target_track.override_steer_force_threshold;
    ffb_dev_threshold_         = config.ffb.target_track.override_steer_dev_threshold;
    ffb_sustain_time_          = config.ffb.target_track.override_sustain_time;
    ffb_target_rate_gate_      = config.ffb.target_track.override_target_rate_gate;
    ffb_derror_rate_gate_      = config.ffb.target_track.override_position_error_rate_gate;
    ffb_wheel_over_target_eps_ = config.ffb.target_track.override_wheel_over_target_epsilon;
    ffb_sustain_accum_         = 0.0;
    ffb_prev_target_norm_      = 0.0;
    ffb_prev_pos_error_        = 0.0;
    ffb_history_valid_         = false;
    ffb_sample_                = {};
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
        ffb_sustain_accum_ = 0.0;
        ffb_history_valid_ = false;
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

    // feature:F7 (F7b) — FFB torque-proxy path. Fires the lateral latch after
    // the servo has been actively pushed against for at least sustain_time.
    // Only meaningful for the LATERAL domain (steering); the pedal path stays
    // exclusive to the direct throttle/brake threshold check above.
    //
    // Detection uses TWO rate gates. Both must be settled (rate below gate)
    // for sustain to accumulate:
    //
    //   1. |d(target)/dt|  < target_rate_gate  (AD not actively steering)
    //   2. |d(dev)/dt|     < derror_rate_gate  (servo not actively catching up)
    //
    // Together these characterise "steady-state deviation" — the only shape
    // consistent with a driver holding the wheel against the servo. Transient
    // states (AD steering, servo warmup, hardware inertia after target step)
    // ALL leave one or both rates non-zero and get suppressed.
    //
    // The Day-1 spike (scripts/ffb_spike/05_torque_proxy.py) calibrated the
    // |u|/|dev| thresholds against a STATIC target + zeroed axis only. Both
    // real-machine bugs sat outside that calibration:
    //   - after commit a43e4c67: false latch during curves / lane-changes
    //     (moving target + tracking lag)      → target_rate_gate closed this
    //   - after commit 549e5823: false latch on straight-drive startup
    //     (static target + wheel-inertia lag) → derror_rate_gate closes this
    //
    // Bootstrap: the very first active sample has no history for either rate.
    // We cannot distinguish "transient in progress" from "steady-state" until
    // we have TWO consecutive samples. Suppress accumulation on the bootstrap
    // frame; the detector arms starting on the 2nd active sample.
    if (lat_configured_manual_ && ffb_sample_.active)
    {
        bool suppress = !ffb_history_valid_ || dt <= 1e-6;

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

        const bool moving_target      = std::abs(target_rate) > ffb_target_rate_gate_;
        const bool tracking_transient = std::abs(derror_rate) > ffb_derror_rate_gate_;
        const bool over_force         = std::abs(ffb_sample_.commanded_force) > ffb_force_threshold_;
        const bool over_dev           = std::abs(ffb_sample_.position_error)  > ffb_dev_threshold_;

        // Third gate (post-f723fa90): the physical wheel must be OPPOSING
        // the target. See ManualDriveConfig for the full rationale — short
        // version: an unheld wheel either sits at 0 (small AD target below
        // G29 breakaway friction) or slowly creeps toward target under
        // sustained servo force (large AD target, wheel gradually catches
        // up over seconds). BOTH regimes keep the wheel same-signed as
        // target and |actual| < |target|; both are servo behavior, not
        // driver behavior. A driver actively taking over either moves the
        // wheel past target (magnitude opposition) or reverses direction
        // (sign opposition). Derive actual_norm from the sample; the sink
        // records position_error = target_norm - actual_norm exactly.
        const double actual_norm    = ffb_sample_.target_norm - ffb_sample_.position_error;
        // The sign-opposition arm additionally requires |actual| >= epsilon:
        // real-G29 right_turn measured on 2026-07-25 shows the physical axis
        // sitting at +0.011 (column noise / mechanical offset) when target
        // is -0.83 and the servo cannot move the stuck wheel. Without the
        // deadzone, that +0.011 counts as "sign opposition" and false-latches
        // just as the pre-fix behavior did. epsilon is 50× the SDL2 noise
        // floor (~0.001 = 32 raw counts out of 32767), safely above hardware
        // jitter but below any deliberate hand movement.
        const bool sign_opposition      = (ffb_sample_.target_norm * actual_norm) < 0.0
                                          && std::abs(actual_norm) >= ffb_wheel_over_target_eps_;
        const bool magnitude_opposition = std::abs(actual_norm) >
                                          std::abs(ffb_sample_.target_norm) + ffb_wheel_over_target_eps_;
        const bool wheel_engaged        = sign_opposition || magnitude_opposition;

        if (suppress || moving_target || tracking_transient)
        {
            // Not steady-state. Reset sustain. Detector re-arms as soon as
            // both rates settle below their gates.
            ffb_sustain_accum_ = 0.0;
        }
        else if ((over_force || over_dev) && wheel_engaged)
        {
            ffb_sustain_accum_ += dt;
            if (ffb_sustain_accum_ >= ffb_sustain_time_)
            {
                lat_active = true;
            }
        }
        else
        {
            // Either signals are quiet or the wheel is at rest — in both
            // cases reset the sustain accumulator so a subsequent real
            // driver push starts fresh.
            ffb_sustain_accum_ = 0.0;
        }
    }
    else
    {
        // Sample missing or servo off: reset accumulator + history so a stale
        // burst can never be revived on the next Configure/enable transition.
        // History is re-armed at the next active sample.
        ffb_sustain_accum_ = 0.0;
        ffb_history_valid_ = false;
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
