#include "gt_esmini/control/manualdrive/OverrideManager.hpp"
#include "gt_esmini/control/manualdrive/ManualDriveConfig.hpp"
#include "gt_esmini/control/common/VehicleCommand.hpp"

#include <algorithm>
#include <cmath>

namespace gt_esmini
{

// feature:F7 re-anchor instrument — floating-point noise floor for "did a
// re-anchor event actually move the shadow". S3/S4 only count/accumulate
// when |delta| exceeds this; S1 (seed) is the deliberate exception and
// always counts (see its call site). Axis-frac deltas here are O(1e-2) to
// O(1), so 1e-9 is many orders below any real event and comfortably above
// double round-off on subtractions of numbers in that range.
constexpr double kReanchorDeltaEps = 1e-9;

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
    resume_edge_                 = false;

    // feature:F7 — startup axis reference (see the Update() site). Re-armed
    // here so a reconfigure begins a fresh session rather than carrying the
    // previous run's reference.
    startup_axis_seen_           = false;
    startup_axis_ref_active_     = false;
    startup_axis_ref_            = 0.0;

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
    ffb_shadow_velocity_tau_   = config.ffb.target_track.override_shadow_velocity_tau;
    ffb_shadow_dead_time_      = config.ffb.target_track.override_shadow_dead_time;
    ffb_shadow_onset_grace_    = config.ffb.target_track.override_shadow_onset_grace;
    ffb_shadow_motion_rate_eps_= config.ffb.target_track.override_shadow_motion_rate_eps;
    ffb_disagree_elapsed_      = 0.0;
    ffb_disagree_active_       = false;
    ffb_sustain_accum_         = 0.0;
    ffb_prev_target_norm_      = 0.0;
    ffb_prev_pos_error_        = 0.0;
    ffb_history_valid_         = false;
    ffb_shadow_norm_           = 0.0;
    ffb_shadow_moving_         = false;
    ffb_shadow_vel_            = 0.0;
    ffb_shadow_valid_          = false;
    ffb_force_history_.clear();
    ffb_sample_                = {};
    ffb_diag_                  = {};

    // feature:F7 re-anchor instrument (observational only; see
    // test_results/f7_reanchor_instrument_spec.md and OverrideManager.hpp).
    reanchor_hard_count_            = 0;
    reanchor_soft_count_            = 0;
    reanchor_hard_delta_abs_accum_  = 0.0;
    reanchor_soft_delta_abs_accum_  = 0.0;
    free_below_real_count_          = 0;
    reanchor_pending_source_   = FfbLatchDiagnostics::ReanchorSource::SEED;
    free_shadow_norm_          = 0.0;
    free_shadow_rest_anchor_   = 0.0;
    free_shadow_moving_        = false;
    free_shadow_valid_         = false;
    free_shadow_vel_           = 0.0;
}

void OverrideManager::UpdateFfbSample(const FfbInterventionSample& sample)
{
    ffb_sample_ = sample;
}

// feature:F7 re-anchor instrument — S2 (plant integration) ONLY, applied to
// the free-running shadow's own state (free_shadow_*). This deliberately
// duplicates the ffb_shadow_* stick-slip/velocity-lag math in Update() below
// verbatim (same physical force `f`, same measured-plant config constants)
// rather than sharing a helper with it: the two shadows must stay fully
// separate instances start to finish (spec §3), and factoring the existing,
// tested detector math into a shared helper would risk changing its
// behavior by 1 bit — which is explicitly forbidden. No onset-grace (S3) or
// drift (S4) re-anchoring is ever applied here; that omission is the entire
// point of this shadow (test_results/f7_reanchor_instrument_spec.md §3).
void OverrideManager::UpdateFreeShadowPlant(double f, double dt, double actual_norm)
{
    if (!free_shadow_moving_)
    {
        // Mirrors the breakaway-band logic at the ffb_shadow_ integration
        // site, but keyed off free_shadow_rest_anchor_ (this instance's own
        // "demonstrably moving" anchor), never ffb_shadow_rest_anchor_.
        const double band_bottom = (f >= 0.0) ? ffb_shadow_breakaway_left_
                                              : ffb_shadow_breakaway_right_;
        const bool observed_moving =
            std::abs(actual_norm - free_shadow_rest_anchor_) > ffb_shadow_motion_eps_;
        if (std::abs(f) >= ffb_shadow_breakaway_ ||
            (std::abs(f) >= band_bottom && observed_moving))
        {
            free_shadow_moving_ = true;
        }
    }
    double shadow_vel = 0.0;
    if (free_shadow_moving_)
    {
        const double excess = std::abs(f) - ffb_shadow_kinetic_;
        if (excess <= 0.0)
        {
            free_shadow_moving_      = false;
            free_shadow_rest_anchor_ = actual_norm;
            free_shadow_vel_         = 0.0;
        }
        else
        {
            const double speed = std::min(ffb_shadow_force_to_vel_ * excess, ffb_shadow_v_max_);
            shadow_vel = (f >= 0.0) ? -speed : speed;
        }
    }
    if (ffb_shadow_velocity_tau_ > 1e-9)
    {
        const double alpha = 1.0 - std::exp(-dt / ffb_shadow_velocity_tau_);
        free_shadow_vel_ += alpha * (shadow_vel - free_shadow_vel_);
    }
    else
    {
        free_shadow_vel_ = shadow_vel;
    }
    free_shadow_norm_ = std::clamp(free_shadow_norm_ + free_shadow_vel_ * dt, -1.0, 1.0);
}

void OverrideManager::Update(const InputFrame& input, double dt)
{
    just_transitioned_to_manual_ = false;
    just_transitioned_to_auto_   = false;
    resume_edge_                 = false;

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

    // feature:F7 — STARTUP AXIS REFERENCE (bookkeeping half; the detector half
    // is at the direct-axis check below, which carries the full rationale).
    //
    // This sits AHEAD of the RESUME early-return on purpose. The reference is
    // "the axis this session started at", which has nothing to do with which
    // buttons were held: if a run begins with RESUME already pressed, the
    // early-return below would otherwise skip this and the SECOND frame would
    // be taken for the first. (Caught by ResumeRequiresRisingEdge.)
    if (input.pedal_steer && lat_configured_manual_)
    {
        const double axis = input.pedal_steer->steering;
        if (!startup_axis_seen_)
        {
            startup_axis_seen_       = true;
            startup_axis_ref_        = axis;
            startup_axis_ref_active_ = std::abs(axis) > steering_threshold_;
        }
        else if (startup_axis_ref_active_ && std::abs(axis) <= steering_threshold_)
        {
            startup_axis_ref_active_ = false;
        }
    }

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
    resume_edge_         = resume_edge;   // observability; see JustPressedResume()

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
        ffb_shadow_vel_    = 0.0;
        ffb_force_history_.clear();
        // feature:F7 re-anchor instrument — S5. Invalidate the free-running
        // shadow at the same cycle boundary as the real one (spec §3:
        // carrying it across a RESUME would measure the next intervention
        // against a meaningless pre-RESUME reference); tag the next S1 seed
        // as RESUME.
        free_shadow_valid_       = false;
        free_shadow_moving_      = false;
        free_shadow_vel_         = 0.0;
        reanchor_pending_source_ = FfbLatchDiagnostics::ReanchorSource::RESUME;
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
            // feature:F7 — STARTUP AXIS REFERENCE.
            //
            // MEASURED (test_results/f7_2x2_final.log, 6-cell probe with the
            // initial axis cross-checked against the DLL's own Configure()
            // log line): a run that begins with the wheel at -0.137 axis-frac
            // (-61.7 deg) latches MANUAL at t=0.01 -- frame 1 -- through this
            // direct-axis check, and self-perpetuates (the latch is one-way,
            // so AD never drives for the rest of the session). The same probe
            // showed a curved start with a centred wheel does NOT latch, so
            // curvature was incidental; the trigger is purely the axis level
            // at t=0.
            //
            // WHY THE LEVEL IS NOT EVIDENCE. The wheel is a physical object
            // that stays wherever the previous session left it, with the servo
            // not yet running to centre it. "Axis is off-centre on frame 1"
            // therefore says nothing about whether a hand is on it -- and the
            // failure is badly asymmetric: a false latch costs the user the
            // entire run, while a missed one costs the few frames it takes the
            // driver to move the wheel a little.
            //
            // So while the wheel starts outside the neutral band, the direct
            // axis path measures CHANGE from where it started rather than the
            // absolute angle. The reference is dropped for good the first time
            // the wheel is observed inside the band, because from then on the
            // "left over from last time" explanation no longer applies.
            //
            // A run that starts with the wheel already in the band never arms
            // the reference at all, so every existing scenario, batch and test
            // that starts centred behaves bit-identically.
            //
            // WHAT STILL CATCHES A GENUINELY HELD WHEEL. The residual/shadow
            // path below, which decides from physics rather than from a level:
            // the same probe's positive control (a wheel held off-centre at
            // -0.034 with the servo running) latched via RESIDUAL_PATH_LATCH.
            // Deferring the ambiguous startup case to that detector routes it
            // to the one that can actually tell a hand from a leftover angle.
            // (The reference itself is armed and dropped above, ahead of the
            // RESUME early-return.)

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
                const double lat_signal = startup_axis_ref_active_
                                              ? ps.steering - startup_axis_ref_
                                              : ps.steering;
                lat_active = std::abs(lat_signal) > steering_threshold_;
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

        // feature:F7 — the PREVIOUS sample's measured axis and tracking error,
        // captured HERE because ffb_prev_target_norm_/ffb_prev_pos_error_ are
        // overwritten with this frame's values a few lines below.
        //
        // This is not a tidy-up. The onset-grace block further down used to
        // recompute `prev_actual` from those two members AFTER the overwrite,
        // so it was subtracting the current sample from itself: its
        // `observed_moving` was identically false on every frame of every run.
        // The test it guards -- "the shadow says moving but the measurement
        // says still, or vice versa" -- therefore collapsed to "the shadow
        // says moving", which is only one of the two directions. The direction
        // that was silently missing (measurement moving while the shadow
        // stands still) is exactly the false-latch case below, so the
        // protection never fired where it was needed, while it kept firing --
        // and erasing real evidence -- where it was not.
        const double prev_actual_measured = ffb_prev_target_norm_ - ffb_prev_pos_error_;
        const double prev_abs_pos_error   = std::abs(ffb_prev_pos_error_);

        // Is the MEASURED wheel moving? (The shadow's own opinion is
        // ffb_shadow_moving_; these two disagreeing is the whole subject of
        // the onset grace and of S7 below.)
        const bool observed_moving_measured =
            !suppress &&
            std::abs((actual_norm - prev_actual_measured) / dt) > ffb_shadow_motion_rate_eps_;

        // Is the servo LOSING the wheel? Every override direction makes this
        // true; a wheel merely carried along by the servo does not. Compared
        // strictly, so no tuning constant enters the decision.
        const bool tracking_error_growing =
            std::abs(ffb_sample_.position_error) > prev_abs_pos_error;

        // feature:F7 re-anchor instrument — per-frame accumulator/tag (spec
        // §2). At most one of {S1 seed} or {S3 onset-grace [+S4 same frame]}
        // can fire in a given frame (S1 only runs while suppress is true,
        // S3/S4 only while suppress is false), so "hard wins over soft" only
        // ever has to arbitrate S3 vs S4 co-firing (see the S4 site below).
        double reanchor_frame_delta = 0.0;
        FfbLatchDiagnostics::ReanchorSource reanchor_frame_source =
            FfbLatchDiagnostics::ReanchorSource::NONE;

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
            const double reanchor_shadow_before = ffb_shadow_norm_;
            ffb_shadow_norm_        = actual_norm;
            ffb_shadow_rest_anchor_ = actual_norm;
            ffb_shadow_moving_      = false;
            ffb_shadow_vel_         = 0.0;
            ffb_shadow_valid_       = true;

            // feature:F7 re-anchor instrument — S1 (seed). Tagged with
            // whatever re-arm reason (S5 RESUME / S6 inactive) most recently
            // invalidated the shadow, or SEED if this is a genesis seed with
            // no preceding re-arm (see reanchor_pending_source_ comment).
            // Unlike S3/S4 below, S1 counts UNCONDITIONALLY (delta or not):
            // an arm/re-arm is itself the countable event, not just whatever
            // displacement it happens to produce (spec, revised).
            reanchor_hard_count_++;
            reanchor_hard_delta_abs_accum_ += std::abs(actual_norm - reanchor_shadow_before);
            reanchor_frame_delta           += actual_norm - reanchor_shadow_before;
            reanchor_frame_source           = reanchor_pending_source_;

            // The free-running shadow re-seeds in lockstep — S1 IS applied
            // there too (spec §3): without a seed it has no reference to
            // integrate from. This is arm/re-arm bookkeeping, not an
            // "erasure" being measured against the real detector.
            free_shadow_norm_        = actual_norm;
            free_shadow_rest_anchor_ = actual_norm;
            free_shadow_moving_      = false;
            free_shadow_vel_         = 0.0;
            free_shadow_valid_       = true;
        }

        // --- Transport delay (dead time) -------------------------------
        // The shadow is driven by the force the wheel felt `dead_time` seconds
        // ago, not the force commanded this frame. Default 0 = disabled, i.e.
        // no behaviour change until the delay has actually been MEASURED (see
        // ManualDriveConfig). The history is kept regardless so enabling it is
        // a config change, not a code change.
        ffb_force_history_.push_back({ffb_sample_.effective_force_signed, dt});
        double f = ffb_sample_.effective_force_signed;
        {
            // Drop anything older than we could ever need, then walk back
            // `dead_time` worth of frames.
            double age = 0.0;
            for (auto it = ffb_force_history_.rbegin(); it != ffb_force_history_.rend(); ++it)
            {
                f = it->force;
                age += it->dt;
                if (age >= ffb_shadow_dead_time_) break;
            }
            double total = 0.0;
            for (const auto& e : ffb_force_history_) total += e.dt;
            while (ffb_force_history_.size() > 1 &&
                   total - ffb_force_history_.front().dt > ffb_shadow_dead_time_ + 0.5)
            {
                total -= ffb_force_history_.front().dt;
                ffb_force_history_.pop_front();
            }
        }

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
                    // Static friction grabs: velocity goes to zero at once, it
                    // does NOT decay through the lag below. Measured: the
                    // "wheel coasts on after the force drops" regime accounts
                    // for ~0.1% of residual growth on the real machine
                    // (residual_decompose.py R4), so modelling it would add
                    // drift for no fidelity.
                    ffb_shadow_vel_ = 0.0;
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
            // --- First-order velocity lag (mechanical inertia) -------------
            // The measured force->velocity curve is a STEADY-STATE map: a
            // constant force was applied and the terminal speed recorded. A
            // real wheel does not reach that speed instantly, and the shadow
            // used the map as if it did. On the 2026-07-26 hands-off runs that
            // cost 0.070s of the 0.100s latch clock on traffic_lights_junction
            // — 30 ms from a false MANUAL latch with nobody touching the wheel.
            //
            // Adding this lag takes the clock to 0.000s on all three measured
            // scenarios. It does NOT make the model correct: the peak residual
            // margin only improves to ~1.2x, so roughly three quarters of the
            // residual is still unexplained by anything in this model family.
            // See test_results/f7_residual_completion_report.md §17.
            if (ffb_shadow_velocity_tau_ > 1e-9)
            {
                const double alpha = 1.0 - std::exp(-dt / ffb_shadow_velocity_tau_);
                ffb_shadow_vel_ += alpha * (shadow_vel - ffb_shadow_vel_);
            }
            else
            {
                ffb_shadow_vel_ = shadow_vel;
            }
            ffb_shadow_norm_ = std::clamp(ffb_shadow_norm_ + ffb_shadow_vel_ * dt, -1.0, 1.0);

            // feature:F7 re-anchor instrument — S2 only, free-running shadow.
            // Must run inside this same `!suppress` guard: skip it exactly
            // when the real shadow also skips integration (bootstrap frame),
            // otherwise the two shadows would desync on frame 1.
            UpdateFreeShadowPlant(f, dt, actual_norm);
        }

        // --- Onset grace (see ManualDriveConfig) --------------------------
        // While the shadow says "moving" and the measurement says "at rest"
        // (or vice versa), we are inside the breakaway band's indeterminacy.
        // Re-sync rather than bank the difference — but only for as long as a
        // transition could plausibly take. A driver's disagreement persists.
        if (!suppress && ffb_shadow_onset_grace_ > 0.0)
        {
            const bool observed_moving = observed_moving_measured;
            if (observed_moving != ffb_shadow_moving_)
            {
                ffb_disagree_elapsed_ = ffb_disagree_active_ ? ffb_disagree_elapsed_ + dt : 0.0;
                ffb_disagree_active_  = true;
                if (ffb_disagree_elapsed_ <= ffb_shadow_onset_grace_)
                {
                    // feature:F7 re-anchor instrument — S3 (onset grace).
                    // Identified by the spec as the dominant erasure
                    // mechanism: no residual gate, hard assignment, and can
                    // repeat every frame the disagreement chatters (no upper
                    // bound on ffb_disagree_elapsed_ resets). Only counted
                    // when it actually moves the shadow (unlike S1, this is
                    // NOT an arm/re-arm event in its own right — see
                    // kReanchorDeltaEps comment).
                    const double reanchor_shadow_before = ffb_shadow_norm_;
                    ffb_shadow_norm_ = actual_norm;
                    if (!observed_moving) ffb_shadow_vel_ = 0.0;
                    const double reanchor_hard_delta = actual_norm - reanchor_shadow_before;
                    if (std::abs(reanchor_hard_delta) > kReanchorDeltaEps)
                    {
                        reanchor_hard_count_++;
                        reanchor_hard_delta_abs_accum_ += std::abs(reanchor_hard_delta);
                        reanchor_frame_delta           += reanchor_hard_delta;
                        reanchor_frame_source           = FfbLatchDiagnostics::ReanchorSource::ONSET_GRACE;
                    }
                }
            }
            else
            {
                ffb_disagree_active_  = false;
                ffb_disagree_elapsed_ = 0.0;
            }
        }

        // --- S7: motion the servo is responsible for is not a hand ---------
        //
        // MEASURED (audit_handsoff_runs/run17 + run35, hands off from start to
        // finish): the wheel moved exactly as much as the AD target every
        // frame, the servo reported effective_force 0.00000 and
        // position_error 0.00000, and the residual still climbed to 0.106 and
        // latched MANUAL.
        //
        // The cause is structural. The shadow answers "where would the wheel
        // be under this force with no hand on it", and a PD servo's force goes
        // to zero exactly when it is tracking well. A force below the
        // breakaway band cannot move the shadow at all -- so the BETTER the
        // servo tracks a moving target, the more completely the shadow stands
        // still while the wheel travels with the target, and the whole of that
        // travel is booked as evidence of a driver. The residual ends up
        // measuring how fast the AD target is moving.
        //
        // The discriminator is the tracking error, not a new threshold. Every
        // override direction the detector must catch -- pressing against the
        // AD command, steering past it, counter-steering, or gripping the
        // wheel to a stop -- makes |position_error| GROW, because the servo
        // can no longer put the wheel where it asked. A wheel merely being
        // carried along has a small, NON-growing error. So when all three of
        //   - the measurement says the wheel is moving,
        //   - the shadow says the force is too small to move it, and
        //   - the tracking error is not growing
        // hold at once, the model is simply wrong about the wheel's state of
        // motion, and the honest correction is to update the model rather than
        // to bank the disagreement as driver evidence.
        //
        // Deliberately NOT time-boxed, unlike the onset grace above. The grace
        // is bounded because a driver's disagreement persists while a
        // breakaway transient does not; but a well-tracked wheel on a long
        // steering correction disagrees for as long as the correction lasts,
        // and it is not evidence at ten seconds any more than at ten
        // milliseconds. Bounding it would only postpone the false latch.
        //
        // Deliberately NOT conditioned on the shadow's own state of motion
        // either. The same argument covers the case where BOTH are moving but
        // at different speeds: a plant whose breakaway/slope differ from the
        // shadow's constants drifts away from it on every long steering
        // correction, and that drift is model error, not a hand. (Until this
        // was fixed the drift was being hidden instead: the onset grace's
        // `observed_moving` was identically false -- see the capture of
        // prev_actual_measured above -- which made the grace fire on every
        // frame the shadow was moving and blanket-erase the disagreement.
        // Removing that accident without putting a principled test in its
        // place would have turned FalsePositive4's hands-off plants red.)
        if (!suppress && observed_moving_measured && !tracking_error_growing)
        {
            const double before = ffb_shadow_norm_;
            ffb_shadow_norm_        = actual_norm;
            ffb_shadow_vel_         = 0.0;
            ffb_shadow_rest_anchor_ = actual_norm;
            const double delta = actual_norm - before;
            if (std::abs(delta) > kReanchorDeltaEps)
            {
                reanchor_hard_count_++;
                reanchor_hard_delta_abs_accum_ += std::abs(delta);
                reanchor_frame_delta           += delta;
                // Hard sources win over the soft drift tag, and this is a hard
                // assignment; it is reported under its own name so the erasure
                // instrument can tell it apart from the onset grace.
                reanchor_frame_source = FfbLatchDiagnostics::ReanchorSource::SERVO_TRACKING;
            }
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
            // feature:F7 re-anchor instrument — S4 (soft drift correction).
            // This branch runs on EVERY frame with residual<=threshold (most
            // of a run, once converged), so it must only count/accumulate
            // when it actually moved the shadow — otherwise reanchor_soft_count
            // degenerates into a frame counter (review finding).
            const double reanchor_shadow_before = ffb_shadow_norm_;
            ffb_shadow_norm_ += alpha * (actual_norm - ffb_shadow_norm_);
            const double reanchor_soft_delta = ffb_shadow_norm_ - reanchor_shadow_before;
            if (std::abs(reanchor_soft_delta) > kReanchorDeltaEps)
            {
                reanchor_soft_count_++;
                reanchor_soft_delta_abs_accum_ += std::abs(reanchor_soft_delta);
                reanchor_frame_delta           += reanchor_soft_delta;
                if (reanchor_frame_source == FfbLatchDiagnostics::ReanchorSource::NONE)
                    reanchor_frame_source = FfbLatchDiagnostics::ReanchorSource::DRIFT;  // hard (S1/S3) wins if already set
            }
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

        // feature:F7 re-anchor instrument (observational only — nothing
        // above this point or below reads these fields back into the latch;
        // see test_results/f7_reanchor_instrument_spec.md §2/§4).
        ffb_diag_.reanchor_hard_count           = reanchor_hard_count_;
        ffb_diag_.reanchor_soft_count           = reanchor_soft_count_;
        ffb_diag_.reanchor_delta                = reanchor_frame_delta;
        ffb_diag_.reanchor_hard_delta_abs_accum = reanchor_hard_delta_abs_accum_;
        ffb_diag_.reanchor_soft_delta_abs_accum = reanchor_soft_delta_abs_accum_;
        ffb_diag_.reanchor_source               = reanchor_frame_source;
        ffb_diag_.free_shadow_norm              = free_shadow_norm_;
        ffb_diag_.free_residual                 = std::abs(actual_norm - free_shadow_norm_);

        // NOT an invariant — an earlier draft asserted free_residual >=
        // residual and PM retracted it: S3 (above) zeroes only the real
        // shadow's velocity, so right after an onset-grace re-sync the two
        // shadows integrate from mismatched hysteresis state and free can
        // transiently read CLOSER to actual than the real, erased shadow.
        // Observe how often that happens instead of asserting it can't. The
        // comparison that actually matters is walk-level max(free_residual)
        // vs max(residual) over the whole run, not per-frame ordering.
        if (ffb_diag_.free_residual < ffb_diag_.residual)
            free_below_real_count_++;
        ffb_diag_.free_below_real_count = free_below_real_count_;
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
        ffb_shadow_vel_    = 0.0;
        ffb_force_history_.clear();
        // feature:F7 re-anchor instrument — S6. Invalidate the free-running
        // shadow in lockstep with the real one; tag the next S1 seed as
        // INACTIVE_REARM (there is no continuity across a servo-off gap).
        free_shadow_valid_       = false;
        free_shadow_moving_      = false;
        free_shadow_vel_         = 0.0;
        reanchor_pending_source_ = FfbLatchDiagnostics::ReanchorSource::INACTIVE_REARM;
        ffb_diag_          = {};
        ffb_diag_.block_reason = FfbLatchDiagnostics::BlockReason::INACTIVE;
        // The cumulative counters are "since run start", not "this frame" —
        // they must survive the {} reset above. reanchor_delta/source and
        // free_shadow_norm/free_residual correctly stay at their {} defaults
        // (no re-anchor event happens, and no shadow position is defined,
        // while inactive), matching the existing actual_norm/shadow_norm/
        // residual zero-on-inactive convention.
        ffb_diag_.reanchor_hard_count           = reanchor_hard_count_;
        ffb_diag_.reanchor_soft_count           = reanchor_soft_count_;
        ffb_diag_.reanchor_hard_delta_abs_accum = reanchor_hard_delta_abs_accum_;
        ffb_diag_.reanchor_soft_delta_abs_accum = reanchor_soft_delta_abs_accum_;
        ffb_diag_.free_below_real_count         = free_below_real_count_;
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
