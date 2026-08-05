// req-vd-ad:REQ-AD-027 / req-vd-ad:REQ-AD-028 / vd-func:FUNC-080
//
// Implementation of the semantics documented in LaneKeepAssist.hpp (sign chain,
// margin-based trigger, judgement/output separation, human steering priority,
// speed band). See that header for the rationale behind every choice below;
// this file only implements it.

#include "gt_esmini/control/manualdrive/LaneKeepAssist.hpp"

#include <algorithm>
#include <cmath>

namespace gt_esmini
{

namespace
{

// Lateral speed below which TLC is not computed. A vehicle that is barely
// moving sideways has a TLC in the hundreds of seconds, and dividing by a
// near-zero rate produces a number whose only real content is floating-point
// noise. Below this the TLC branch simply does not fire and the MARGIN branch
// is what decides -- which is the correct division of labour: TLC is about
// "will cross soon", margin is about "is already close".
constexpr double kMinLateralSpeedForTlc = 0.02;  // [m/s]

}  // namespace

double ApplyLkaCorrectionEnvelope(double raw, double prev, double max_amplitude, double max_rate, double dt)
{
    // Amplitude first: the assist may never ask for more than its authority,
    // whatever the law computed. A non-positive max_amplitude means "no
    // authority", which yields exactly 0.0 rather than an unbounded value --
    // the safe reading of a misconfigured limit, and the one that makes
    // correction_max=0 a usable way to disable the output without disabling the
    // judgement.
    const double amp = std::max(0.0, max_amplitude);
    double       out = std::clamp(raw, -amp, amp);

    // Rate second, anchored on what was ACTUALLY applied last frame. dt <= 0 or
    // a non-positive max_rate leaves the amplitude-clamped value alone: no rate
    // can be attributed to a non-positive time step, and a caller that does not
    // want a rate limit must not get a hidden one.
    if (dt > 0.0 && max_rate > 0.0)
    {
        const double step = max_rate * dt;
        out               = std::clamp(out, prev - step, prev + step);
    }

    // AMPLITUDE AGAIN, AND THIS SECOND PASS IS NOT REDUNDANT.
    //
    // The first version of this function did not have it, on the reasoning that
    // "the rate stage only contracts toward `prev`, and `prev` was itself
    // amplitude-clamped when it was produced". That reasoning is FALSE whenever
    // `prev` is outside the CURRENT bound -- which happens the moment the bound
    // changes (a reloaded config, or max_amplitude driven to 0 to disable the
    // output while leaving the judgement running). Caught by
    // ZeroAmplitudeYieldsExactlyZeroNotUnbounded: with max_amplitude=0 and
    // prev=0.5 the rate stage pulled the result back out to 0.48, i.e. the
    // "authority limit" was whatever the previous frame happened to be.
    //
    // This is the SAME asymmetry AdSteeringEnvelope's jerk stage got wrong and
    // is shipped disabled for: a later shaping stage must never be able to push
    // a command back outside the safety cap an earlier stage imposed. Here it is
    // fixed rather than shipped-disabled, because the fix is available: the
    // amplitude bound is a hard authority limit and the rate limit is comfort,
    // so when they conflict the amplitude wins. In every ordinary frame (`prev`
    // already inside the bound) this second clamp is a no-op and the rate limit
    // is fully intact; it bites only where a bound was tightened underneath a
    // standing value, and there a one-frame snap to the new bound is the correct
    // behaviour, not a defect.
    out = std::clamp(out, -amp, amp);

    return out;
}

bool ManualLkaArbitrates(bool owns_lateral, bool lka_enabled, bool warning_only)
{
    return owns_lateral && lka_enabled && !warning_only;
}

LkaFrameOutput ComputeLaneKeepAssist(const LaneKeepAssistConfig& cfg,
                                     const LkaFrameInput&        in,
                                     double                      dt,
                                     LaneKeepAssistState&        state)
{
    LkaFrameOutput out;
    out.steer_out = in.driver_steering;  // the assist adds; it never replaces

    // ------------------------------------------------------------------
    // Gate: config off, domain not owned, or no usable lane geometry.
    // ------------------------------------------------------------------
    // Nothing is measured and NO cross-frame state is advanced -- the latches
    // are left frozen rather than evolved on data the assist is refusing to act
    // on, so a stretch of not-owned or geometry-less frames cannot quietly
    // decide the first owned frame after it. Same rule AdasCoexistenceStack
    // applies to the kickdown/arbitrator latches on its own bypass.
    //
    // `lane_valid` is part of this gate deliberately: a frame with no resolvable
    // lane is not a frame with a centred vehicle, and treating it as one would
    // make the assist confidently quiet exactly where it has no information.
    if (!cfg.enabled || !in.owns_lateral || !in.lane_valid)
    {
        return out;
    }

    out.evaluated = true;

    // ------------------------------------------------------------------
    // Steering-rate suppression latch (design §5-3). Updated BEFORE the
    // judgement so `state.prev_driver_steering` advances on every evaluated
    // frame regardless of which branch the judgement takes.
    // ------------------------------------------------------------------
    if (dt > 0.0 && state.prev_steering_valid)
    {
        const double rate = std::fabs(in.driver_steering - state.prev_driver_steering) / dt;
        if (rate >= cfg.steer_override_rate)
        {
            // Re-arm the hold on every qualifying frame, so a sustained input
            // keeps the assist off for hold_s past its END, not past its start.
            state.steer_override_timer = std::max(0.0, cfg.steer_override_hold_s);
        }
        else if (state.steer_override_timer > 0.0)
        {
            state.steer_override_timer = std::max(0.0, state.steer_override_timer - dt);
        }
    }
    state.prev_driver_steering = in.driver_steering;
    state.prev_steering_valid  = true;

    out.suppressed_steer     = state.steer_override_timer > 0.0;
    out.suppressed_indicator = in.indicator_active;

    // ==================================================================
    // JUDGEMENT -- runs identically in every mode. Nothing below reads
    // cfg.warning_only; that is REQ-AD-027 step f's structural guarantee
    // (see the header's own block), not an accident of ordering.
    // ==================================================================
    const double abs_offset = std::fabs(in.lane_offset_m);
    out.margin_m            = in.lane_half_width_m - abs_offset - in.vehicle_half_width_m;

    // Speed band (REQ-AD-027 step e). Outside it the function is STANDBY, not
    // UNAVAILABLE -- see the header. The departure latch is also RELEASED here:
    // carrying a latched departure across a band exit would make the assist
    // resume mid-correction on re-entry, from a judgement taken at a speed at
    // which it was not allowed to judge.
    const bool above_min = in.ego_speed_mps >= cfg.min_speed_mps;
    const bool below_max = (cfg.max_speed_mps <= 0.0) || (in.ego_speed_mps <= cfg.max_speed_mps);
    out.in_speed_band    = above_min && below_max;
    if (!out.in_speed_band)
    {
        state.departing_latched = false;
        state.prev_correction   = 0.0;
        return out;
    }

    // Time to line crossing, computed ONLY while moving outward. "Outward" is
    // decided by the SIGNS agreeing: a positive (left-of-centre) offset with a
    // positive (moving-left) lateral speed is a vehicle heading for the left
    // line. A vehicle that is off-centre but coming back has no crossing to be
    // early for, and giving it a TLC would fire the assist against a driver who
    // is already correcting.
    //
    // AT EXACTLY ZERO OFFSET the sign test cannot decide a direction, so the
    // lateral speed's own sign is used: a centred vehicle moving sideways is
    // heading for the line on that side, and the margin is symmetric there
    // anyway.
    const bool moving_outward =
        (std::fabs(in.lateral_speed_mps) > kMinLateralSpeedForTlc) &&
        (abs_offset < 1e-9 || (in.lane_offset_m * in.lateral_speed_mps) > 0.0);

    if (moving_outward && out.margin_m > 0.0)
    {
        out.tlc_s = out.margin_m / std::fabs(in.lateral_speed_mps);
    }
    else if (moving_outward)
    {
        // Already past the line while still moving outward: TLC is zero, not
        // negative and not "unavailable". Reporting -1 here would let the
        // matcher-facing stream say "no crossing expected" about a vehicle that
        // has already crossed.
        out.tlc_s = 0.0;
    }

    // Engage / release with hysteresis (see the config fields). The TLC arm and
    // the margin arm are OR'd on engage; release requires BOTH to have cleared,
    // otherwise a vehicle sitting just outside the margin band with a standing
    // TLC would flicker.
    const bool tlc_arm    = (out.tlc_s >= 0.0) && (out.tlc_s <= cfg.tlc_threshold_s);
    const bool margin_arm = out.margin_m <= cfg.margin_threshold_m;

    if (!state.departing_latched)
    {
        state.departing_latched = tlc_arm || margin_arm;
    }
    else if (out.margin_m >= cfg.release_margin_m && !tlc_arm)
    {
        state.departing_latched = false;
    }
    out.departing = state.departing_latched;

    // The WARNING (LDW, and gt.lka.warning) is the departure verdict minus the
    // indicator suppression only -- see the header's HUMAN STEERING PRIORITY
    // block for why the steering-rate suppression does NOT silence it.
    out.warning = out.departing && !out.suppressed_indicator;

    // req-vd-ad:REQ-AD-028 段b: the steering-origin override. Raised while the
    // driver's steering input is holding the assist off -- and DELIBERATELY NOT
    // additionally conditioned on a departure standing this frame.
    //
    // WHY THE WIDE CONDITION (and it was measured, not reasoned). The first
    // version here was `suppressed_steer && departing`, on the reading that
    // there has to BE an assist to hold off. Running md_lka_human_steer showed
    // what that costs: while the driver steers across the road the vehicle
    // passes through the CENTRE of each lane it crosses, and on the one frame it
    // did (t=7.30, offset 0.115 m) the departure verdict went false and the
    // override channel blinked off for 50 ms -- with the driver's hand doing
    // exactly the same thing before and after.
    //
    // That is precisely the failure mode phase B rejected for the accelerator
    // producer: OSI's DriverOverride asks whether the driver has overridden the
    // FUNCTION, which is a property of their INPUT, not of one frame's geometry.
    // AEB reports overridden for as long as the kickdown holds, whether or not a
    // target happens to be in front; LKA reports overridden for as long as the
    // steering suppression holds, whether or not a departure happens to be in
    // front. Both narrow facts remain separately observable -- gt.aeb.suppressed
    // there, gt.lka.departure here -- so nothing is lost by reporting the wide
    // one in the channel whose semantics it fits.
    out.driver_override_steering = out.suppressed_steer;

    // ==================================================================
    // OUTPUT -- warning_only never enters this block. It is fenced here, in
    // one place, on purpose: there is no second path to `correction`.
    // ==================================================================
    if (cfg.warning_only)
    {
        state.prev_correction = 0.0;
        return out;
    }

    // A SUPPRESSION IS AN IMMEDIATE STOP, NOT A RAMP-DOWN.
    //
    // REQ-AD-027 step b says "介入しない/即時中断する" and design §5-3 repeats
    // it: while a suppression stands the assist produces NOTHING, on the very
    // frame the suppression starts. An earlier version of this file released
    // through the rate limiter here, reasoning that letting go smoothly is
    // kinder to the driver's hands. It is -- and it is also the wrong function:
    // a rate-limited release keeps the assist pushing for several frames
    // against a driver who has just declared, with the indicator or with the
    // wheel, that they are steering deliberately. The requirement's word is
    // 即時, and a "human steering priority" that persists for 0.2 s is not one.
    //
    // (Caught by DeliberateSteeringSuppressesTheCorrectionButNotTheWarning,
    // which measured 0.020 of correction on the frame after the driver's input.)
    //
    // The anchor is zeroed with the output so the NEXT engagement ramps up from
    // zero rather than resuming from a value the driver has already refused.
    if (out.suppressed_indicator || out.suppressed_steer)
    {
        state.prev_correction = 0.0;
        return out;  // correction/correcting stay at their defaults, steer_out = driver's own
    }

    if (!out.departing)
    {
        // Not a suppression -- the departure simply ended (the vehicle is back
        // inside the hysteresis band). Here the ramp-down IS right: nobody has
        // objected to the assist, and dropping its contribution in one frame is
        // felt as the assist letting go with a jolt -- the failure mode
        // AdSteeringEnvelope's own rate stage exists to prevent on the AD side.
        const double released = ApplyLkaCorrectionEnvelope(0.0,
                                                           state.prev_correction,
                                                           cfg.correction_max,
                                                           cfg.correction_rate_max,
                                                           dt);
        state.prev_correction = released;
        out.correction        = released;
        out.correcting        = std::fabs(released) > 0.0;
        out.steer_out         = std::clamp(in.driver_steering + released, -1.0, 1.0);
        return out;
    }

    // The correction law. POSITIVE gains on vehicle-left-positive inputs give a
    // positive (steer-right) correction for a leftward drift -- see the
    // header's SIGN CHAIN block, and do not "fix" the signs here without
    // re-reading it.
    const double raw = cfg.kp_offset * in.lane_offset_m + cfg.kd_lateral * in.lateral_speed_mps;

    const double applied = ApplyLkaCorrectionEnvelope(raw,
                                                      state.prev_correction,
                                                      cfg.correction_max,
                                                      cfg.correction_rate_max,
                                                      dt);
    state.prev_correction = applied;

    out.correction = applied;
    out.correcting = std::fabs(applied) > 0.0;
    // The SUM is clamped to the physical command range, but the assist's own
    // contribution is reported unclamped-by-the-sum in `correction`: a matcher
    // asking "did the function steer" must read the function's own output, not
    // a number the driver's input could have saturated away (the same reason
    // no_brake_output reads gt.msl.brake_out rather than the vehicle's brake).
    out.steer_out = std::clamp(in.driver_steering + applied, -1.0, 1.0);

    return out;
}

}  // namespace gt_esmini
