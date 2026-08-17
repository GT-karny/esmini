// req-vd-ad:REQ-AD-026 / req-vd-ad:REQ-AD-031 / vd-func:FUNC-079
//
// Implementation of the semantics documented in AccLonController.hpp (state
// machine, one-point policy ceiling, speed loop, Stop&Go hold, the two
// DriverOverride producers). See that header for the rationale behind every
// choice below; this file only implements it.

#include "gt_esmini/control/manualdrive/AccLonController.hpp"

#include <algorithm>
#include <cmath>

namespace gt_esmini
{

namespace
{

double Clamp01(double v)
{
    return std::min(1.0, std::max(0.0, v));
}

}  // namespace

double AccThwStages::AtStage(int stage) const
{
    switch (stage)
    {
        case 0:
            return short_s;
        case 2:
            return long_s;
        default:
            return mid_s;  // clamps any out-of-range index onto the middle stage
    }
}

AdasOperations DecodeAdasOperations(std::uint32_t buttons, std::uint32_t prev_buttons)
{
    auto rising = [&](std::uint32_t bit) { return (buttons & bit) != 0u && (prev_buttons & bit) == 0u; };

    AdasOperations ops;
    ops.acc_toggle     = rising(ButtonBits::ACC_TOGGLE);
    ops.acc_set_resume = rising(ButtonBits::ACC_SET_RESUME);
    ops.acc_speed_up   = rising(ButtonBits::ACC_SPEED_UP);
    ops.acc_speed_down = rising(ButtonBits::ACC_SPEED_DOWN);
    ops.acc_thw_cycle  = rising(ButtonBits::ACC_THW_CYCLE);
    ops.msl_toggle     = rising(ButtonBits::MSL_TOGGLE);
    return ops;
}

AccCeiling EvaluateAccCeiling(const std::vector<PolicyConstraint>& constraints, double decel_max_mps2)
{
    AccCeiling out;
    // A non-positive decel budget would make the kinematic ceiling either
    // divide by zero or claim a stop needs no distance. Guard with a small
    // positive floor rather than skipping stop constraints: skipping would
    // silently turn a stop request into "no ceiling", which is the dangerous
    // direction.
    const double a = std::max(0.1, decel_max_mps2);

    for (const auto& c : constraints)
    {
        // design §3-1: the safety stage owns SAFETY-tier constraints. See the
        // header's EvaluateAccCeiling doc for why folding them in here would
        // destroy REQ-AD-026 step d's separability.
        if (c.tier == PolicyConstraint::Tier::SAFETY) continue;

        switch (c.kind)
        {
            case PolicyConstraint::Kind::MAX_SPEED:
            case PolicyConstraint::Kind::MAX_SPEED_TO_S:
                out.ceiling_mps = std::min(out.ceiling_mps, std::max(0.0, c.value));
                break;

            case PolicyConstraint::Kind::STOP_AT_S:
            {
                const double d       = std::max(0.0, c.s);
                const double v_allow = std::sqrt(2.0 * a * d);
                out.ceiling_mps      = std::min(out.ceiling_mps, v_allow);
                if (!out.stop_requested || d < out.stop_distance_m)
                {
                    out.stop_distance_m = d;
                }
                out.stop_requested = true;
                break;
            }

            default:
                // YIELD / WAIT_UNTIL / NONE carry no speed ceiling of their own.
                // No manual-stack policy emits them today; ignoring rather than
                // guessing keeps this function's contract narrow.
                break;
        }
    }
    return out;
}

AccLonController::AccLonController(const AccLonControllerConfig& cfg) : cfg_(cfg)
{
    thw_stage_ = std::min(AccThwStages::kStageCount - 1, std::max(0, cfg_.thw_default_stage));
}

void AccLonController::ApplyOperations(const AccFrameInput& in)
{
    // ---- power toggle -----------------------------------------------------
    if (in.ops.acc_toggle)
    {
        if (state_ == AccState::OFF)
        {
            state_ = AccState::STANDBY;
        }
        else
        {
            // Powering OFF forgets the remembered setting: a real stalk's
            // resume memory does not survive the function being switched off,
            // and keeping it would let a RESUME after a power cycle restore a
            // speed the driver set in a completely different situation.
            state_         = AccState::OFF;
            has_set_speed_ = false;
            set_speed_mps_ = 0.0;
        }
    }

    if (state_ == AccState::OFF) return;

    // ---- set / resume -----------------------------------------------------
    // Same button, two meanings, decided by whether a setting is remembered
    // (header's state-machine block).
    if (in.ops.acc_set_resume && state_ == AccState::STANDBY)
    {
        if (!has_set_speed_)
        {
            set_speed_mps_ = std::max(0.0, in.ego_speed_mps);  // SET: current speed becomes the target
            has_set_speed_ = true;
        }
        state_ = AccState::ACTIVE;  // RESUME when a setting already exists
    }

    // ---- setting adjustment (REQ-AD-026 step e) ---------------------------
    // Allowed in STANDBY as well as ACTIVE: a driver adjusting the setting
    // before resuming is ordinary use, and refusing it in STANDBY would make
    // the remembered-setting semantics above half-useful.
    if (has_set_speed_ && (in.ops.acc_speed_up || in.ops.acc_speed_down))
    {
        const double delta = in.ops.acc_speed_up ? cfg_.set_speed_step_mps : -cfg_.set_speed_step_mps;
        const double next  = std::max(0.0, set_speed_mps_ + delta);
        if (next != set_speed_mps_)
        {
            set_speed_mps_        = next;
            setting_ever_changed_ = true;
        }
    }

    // ---- following-distance stage (REQ-AD-026 step h) ---------------------
    if (in.ops.acc_thw_cycle)
    {
        thw_stage_            = (thw_stage_ + 1) % AccThwStages::kStageCount;
        setting_ever_changed_ = true;
    }
}

AccFrameOutput AccLonController::Step(const AccFrameInput& in, double dt)
{
    AccFrameOutput out;

    // A disabled function has no state machine at all: it must be
    // indistinguishable from "was never built" at the output, so operations
    // are not even decoded (a stray ACC_TOGGLE in an ops profile must not
    // quietly arm a function the config switched off).
    if (!cfg_.enabled)
    {
        state_                  = AccState::OFF;
        stop_hold_              = false;
        brake_override_latched_ = false;
        integral_               = 0.0;
        out.state               = AccState::OFF;
        out.throttle            = in.driver_throttle;
        out.brake               = in.driver_brake;
        out.thw_setting_s       = cfg_.thw_stages.AtStage(thw_stage_);
        out.thw_actual_s        = in.thw_actual_s;
        out.thw_stage           = thw_stage_;
        return out;
    }

    ApplyOperations(in);

    // ---- availability band (REQ-AD-026 step f) ----------------------------
    // Evaluated AFTER the operations so a set/resume pressed outside the band
    // is refused in the same frame it was pressed, rather than producing one
    // frame of ACTIVE before the band notices.
    const bool below_band = in.ego_speed_mps < cfg_.min_speed_mps;
    const bool above_band = cfg_.max_speed_mps > 0.0 && in.ego_speed_mps > cfg_.max_speed_mps;
    const bool out_of_band = below_band || above_band;
    if (out_of_band && state_ == AccState::ACTIVE)
    {
        // Demoted, not switched off, and the setting is KEPT: step f requires
        // that re-entering the band allows a resume back to the same target.
        state_    = AccState::STANDBY;
        stop_hold_ = false;
        integral_ = 0.0;
    }

    // ---- driver brake: cancel + the REASON_BRAKE_PEDAL producer -----------
    // The latch (not the edge) is what reaches OSI: the function is overridden
    // for as long as the pedal holds it off. See AccFrameOutput's own comment
    // and phase B's identical argument for the kickdown latch.
    const bool braking = in.driver_brake >= cfg_.brake_cancel_threshold;
    if (braking && state_ == AccState::ACTIVE)
    {
        state_                  = AccState::STANDBY;
        stop_hold_              = false;
        integral_               = 0.0;
        brake_override_latched_ = true;
    }
    if (!braking)
    {
        brake_override_latched_ = false;
    }

    // ---- exclusivity demotion (design §6) ---------------------------------
    if (in.suspended && state_ == AccState::ACTIVE)
    {
        state_     = AccState::STANDBY;
        stop_hold_ = false;
        integral_  = 0.0;
    }

    out.state             = state_;
    out.set_speed_mps     = has_set_speed_ ? set_speed_mps_ : 0.0;
    out.thw_setting_s     = cfg_.thw_stages.AtStage(thw_stage_);
    out.thw_actual_s      = in.thw_actual_s;
    out.thw_stage         = thw_stage_;
    out.driver_override_brake = brake_override_latched_;

    // ---- effective cap (REQ-AD-026 steps a/g) -----------------------------
    // Computed even when not ACTIVE so the observable is continuous: a matcher
    // reading gt.acc.effective_cap_mps across a cancel/resume must not see the
    // key vanish and reappear (absent-is-not-zero cuts both ways -- a key that
    // is absent only sometimes cannot be read as a time series).
    double cap = has_set_speed_ ? set_speed_mps_ : kAccNoCeiling;
    cap        = std::min(cap, in.policy.ceiling_mps);
    if (cfg_.respect_speed_limit && in.speed_limit_mps > 0.0)
    {
        cap = std::min(cap, in.speed_limit_mps);
    }
    out.effective_cap_mps = std::isinf(cap) ? 0.0 : cap;

    if (state_ != AccState::ACTIVE)
    {
        out.throttle = in.driver_throttle;
        out.brake    = in.driver_brake;
        return out;
    }

    out.engaged = true;

    const bool sng = cfg_.stop_and_go.enabled;

    // ---- Stop&Go hold (REQ-AD-031 段a) ------------------------------------
    //
    // THE HOLD IS CONSULTED BEFORE THE ACCELERATOR OVERRIDE, AND THE ORDER IS
    // LOAD-BEARING. Two different thresholds can end a hold -- the generic
    // temporary-override threshold (accel_override_threshold, 0.05 by default)
    // and Stop&Go's own restart threshold (restart_accel_threshold, 0.10) --
    // and the override one is necessarily the LOWER of the two, because its
    // job is to notice any deliberate pedal press at all. Testing the override
    // first would therefore mean the hold ended at 0.05 no matter what
    // restart_accel_threshold said: REQ-AD-031 段a's calibrated restart
    // trigger would be dead config, silently, with the only symptom being that
    // changing it does nothing. Consulting the hold first gives each threshold
    // the regime it was written for -- restart while stopped, override while
    // moving.
    //
    // The hold releases by FALLING THROUGH rather than returning, so the very
    // frame that ends it is still free to be an accelerator override below:
    // the driver who just pressed the pedal hard enough to restart is, in that
    // same instant, a driver asking for their own throttle.
    if (sng && stop_hold_)
    {
        // Held until the human asks for it to end -- this controller never
        // decides to restart (段c/d are future work, design §4-4).
        if (in.driver_throttle >= cfg_.stop_and_go.restart_accel_threshold)
        {
            stop_hold_ = false;
        }
        else
        {
            out.throttle  = 0.0;
            out.brake     = Clamp01(cfg_.stop_and_go.hold_brake);
            out.stop_hold = true;
            return out;
        }
    }

    // ---- accelerator temporary override (design §3-1 step 1) --------------
    // NOT a state transition (header). The human's throttle wins and ACC's
    // brake generation is suppressed; releasing returns to following with no
    // resume. The integrator is dropped so the loop does not resume with a
    // stale term wound up from before the override.
    if (in.driver_throttle >= cfg_.accel_override_threshold)
    {
        out.driver_override_accel = true;
        out.throttle              = in.driver_throttle;
        out.brake                 = 0.0;
        out.engaged               = false;
        stop_hold_                = false;
        integral_                 = 0.0;
        return out;
    }

    if (sng && in.policy.stop_requested && in.ego_speed_mps <= cfg_.stop_and_go.stop_speed_eps_mps)
    {
        stop_hold_    = true;
        integral_     = 0.0;
        out.throttle  = 0.0;
        out.brake     = Clamp01(cfg_.stop_and_go.hold_brake);
        out.stop_hold = true;
        return out;
    }

    // ---- speed loop (design §4-2) -----------------------------------------
    const double v_target = std::isinf(cap) ? in.ego_speed_mps : std::max(0.0, cap);
    const double err      = v_target - in.ego_speed_mps;

    if (std::fabs(err) <= cfg_.speed_deadband_mps)
    {
        // Inside the deadband: hold neither pedal and bleed the integrator
        // toward zero rather than freezing it, so a long cruise does not store
        // authority that fires the moment the error leaves the band.
        integral_ = 0.0;
        out.throttle = 0.0;
        out.brake    = 0.0;
        return out;
    }

    // The loop commands an ACCELERATION, which is then converted to a pedal
    // (see the config's "Pedal references" block for why that indirection is
    // what makes accel_max/decel_max real limits rather than decoration).
    const double accel_budget = std::max(0.05, cfg_.accel_max_mps2);
    const double decel_budget = std::max(0.05, cfg_.decel_max_mps2);

    // Anti-windup: the integrator only accumulates while the resulting command
    // is not already against the envelope -- same discipline as
    // PedalArbitrator's PI, expressed in the acceleration domain.
    const double p_term      = cfg_.speed_kp * err;
    const double candidate_i = (dt > 0.0) ? integral_ + cfg_.speed_ki * err * dt : integral_;
    double       a_cmd       = p_term + candidate_i;
    if (a_cmd > -decel_budget && a_cmd < accel_budget)
    {
        integral_ = candidate_i;
    }
    else
    {
        a_cmd = p_term + integral_;
    }
    a_cmd = std::min(accel_budget, std::max(-decel_budget, a_cmd));

    if (a_cmd >= 0.0)
    {
        out.throttle = Clamp01(a_cmd / std::max(0.1, cfg_.full_throttle_accel_mps2));
        out.brake    = 0.0;
    }
    else
    {
        out.throttle = 0.0;
        out.brake    = Clamp01(-a_cmd / std::max(0.1, cfg_.full_brake_decel_mps2));
    }

    return out;
}

}  // namespace gt_esmini
