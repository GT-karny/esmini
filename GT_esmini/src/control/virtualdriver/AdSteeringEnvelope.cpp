#include "gt_esmini/control/virtualdriver/AdSteeringEnvelope.hpp"

#include <algorithm>
#include <cmath>

namespace gt_esmini
{

double ComputeAdSteeringEnvelope(double                          steer_norm_cmd,
                                 double                          v,
                                 double                          wheel_base,
                                 double                          max_steer_angle,
                                 double                          dt,
                                 const AdSteeringEnvelopeState&  state,
                                 const AdSteeringEnvelopeConfig& cfg,
                                 AdSteeringEnvelopeSnapshot*     out_snapshot)
{
    if (out_snapshot) *out_snapshot = AdSteeringEnvelopeSnapshot{};

    if (!cfg.enabled)
    {
        // Bit-identical no-op on the RETURN VALUE, but steer_norm_in/out must
        // still echo the pass-through — telemetry consumers read them
        // unconditionally, and a stale 0.0 here would misread as "the
        // envelope clipped this to zero" instead of "the envelope did
        // nothing" (see header doc: valid=false / nothing active is how
        // "disabled" is actually signaled).
        if (out_snapshot)
        {
            out_snapshot->steer_norm_in  = steer_norm_cmd;
            out_snapshot->steer_norm_out = steer_norm_cmd;
        }
        return steer_norm_cmd;
    }

    const double wb  = std::max(wheel_base, 1.0e-3);
    const double msa = std::max(max_steer_angle, 1.0e-6);
    const double dt_safe = std::max(dt, 0.0);

    // v_floor is also guarded against a misconfigured (<=0) value here — this
    // must never divide by zero regardless of what the config says.
    const double v_eff = std::max({v, cfg.v_floor, 1.0e-6});

    const double kappa_lat_max = cfg.a_lat_max_steer / (v_eff * v_eff);
    const double kappa_yaw_max = cfg.yaw_rate_max / v_eff;
    const double kappa_max     = std::min(kappa_lat_max, kappa_yaw_max);

    // Same normalized<->radian<->curvature relation PIDPurePursuitDriver uses,
    // inverted (see header doc).
    const double delta_cmd = steer_norm_cmd * msa;
    const double kappa_cmd = std::tan(delta_cmd) / wb;

    // Whichever cap is the tighter (smaller) one is the constraint actually
    // responsible for any clipping below; a tie attributes to both.
    const bool lat_binding    = kappa_lat_max <= kappa_yaw_max;
    const bool yaw_binding    = kappa_yaw_max <= kappa_lat_max;
    const bool kappa_clipped  = std::fabs(kappa_cmd) > kappa_max;

    const double kappa_lim           = std::clamp(kappa_cmd, -kappa_max, kappa_max);
    const double delta_after_lat_yaw = std::atan(kappa_lim * wb);

    // Steering-rate limit, anchored on the last ACTUALLY applied command (see
    // header — the caller owns updating state.prev_steer_norm).
    const double delta_prev = state.prev_steer_norm * msa;
    const double max_step   = cfg.steer_rate_max * dt_safe;
    double       delta_lo   = delta_prev - max_step;
    double       delta_hi   = delta_prev + max_step;
    // Evaluated against this ORIGINAL (pre-jerk-narrowing) window, so that
    // steer_rate_active and steer_jerk_active (below) attribute a clip to
    // whichever constraint actually did it, even though delta_lo/delta_hi are
    // narrowed further by the jerk stage before delta_final is computed.
    const bool rate_clipped = (delta_after_lat_yaw < delta_lo) || (delta_after_lat_yaw > delta_hi);

    // Steering-JERK limit (feature:F7): a further narrowing of the rate
    // window above, not a separate rate limiter. cfg.steer_jerk_max <= 0 is a
    // bit-identical no-op onto the rate-only code path above (required for
    // the regression baseline's deviation=0 check).
    //
    // rate_prev_rad (the realized rate anchor, converted to the rad domain)
    // is CLAMPED to +-steer_rate_max before use, for two reasons:
    //   1. The REALIZED rate can exceed steer_rate_max while a HUMAN is
    //      steering (the caller records the raw manual command via
    //      UpdateAdSteeringEnvelopeState, which has no rate limit of its
    //      own). Without this clamp, an AUTO_RESUME right after a large
    //      manual input would force the envelope to keep ramping at that
    //      stale realized rate for |rate|/jerk seconds before the jerk
    //      limiter could even begin to pull it back down. The clamp caps
    //      that forced continuation at steer_rate_max/steer_jerk_max
    //      (~0.098s with the shipped defaults, ~0.12 in normalized steering)
    //      instead of the unbounded raw rate.
    //   2. It guarantees the jerk window is never empty: whenever
    //      |rate_prev_rad| <= steer_rate_max, delta_prev + rate_prev_rad*dt
    //      falls inside BOTH the original rate window and the jerk window,
    //      so delta_lo <= delta_hi always holds and the std::clamp below
    //      never sees an inverted (empty) range.
    bool jerk_clipped = false;
    if (cfg.steer_jerk_max > 0.0 && dt_safe > 0.0)
    {
        const double rate_prev_rad = std::clamp(state.prev_steer_rate_norm * msa,
                                                -cfg.steer_rate_max, cfg.steer_rate_max);
        const double jerk_step_rad = cfg.steer_jerk_max * msa * dt_safe;  // [rad/s] per frame
        const double jerk_lo = delta_prev + (rate_prev_rad - jerk_step_rad) * dt_safe;
        const double jerk_hi = delta_prev + (rate_prev_rad + jerk_step_rad) * dt_safe;
        jerk_clipped = (delta_after_lat_yaw < jerk_lo) || (delta_after_lat_yaw > jerk_hi);
        delta_lo = std::max(delta_lo, jerk_lo);
        delta_hi = std::min(delta_hi, jerk_hi);
    }

    double delta_final = std::clamp(delta_after_lat_yaw, delta_lo, delta_hi);

    // The curvature cap is applied LAST, and that ordering is the whole point.
    //
    // Everything above shapes HOW FAST the command may move; this bounds WHERE
    // it may be. The rate and jerk windows are centred on delta_prev, so when
    // delta_prev sits outside the curvature-safe zone (a human held the wheel
    // past it, or speed rose and tightened kappa_max under a standing angle),
    // clamping into those windows lands delta_final at the window EDGE — still
    // outside the cap that delta_after_lat_yaw was carefully clamped to. The
    // shaping term then silently defeats the safety term.
    //
    // That is not hypothetical. Measured over a 112-cell AUTO_RESUME sweep at
    // dt=0.01, curvature computed straight from this function's own output
    // against kappa_max, BEFORE this clamp existed:
    //     steer_jerk_max=50    1.2695x
    //     steer_jerk_max=25    1.7719x
    //     steer_jerk_max=10    3.5781x
    // Monotonically worse the tighter the cap, because a tighter cap takes
    // longer to walk delta_prev back inside — sustained 39-176 consecutive
    // frames at 10.5-11.9 m/s, with lateral_accel_active reporting true
    // throughout: the envelope announcing it was clamping while its output sat
    // at twice the limit it enforces.
    //
    // The rate limiter ALONE (steer_jerk_max=0, i.e. the configuration shipped
    // before the jerk stage existed) does NOT reach outside the envelope. An
    // earlier revision of this comment claimed it did, at 1.0078x over 17/28
    // cells; that number was an artifact of the verification script, which
    // (a) built kappa_max from the lateral-accel term only, dropping the min()
    // against the yaw-rate term, and (b) compared against a kappa_max computed
    // from the frame's REPORTED speed, while the envelope had used the
    // previous frame's — telemetry reports speed after that frame's physics
    // integration, so in a mid-acceleration scenario the mismatch is
    // systematic, not noise. Corrected, the rate-limiter-only worst case is
    // exactly 1.0. The hole is structural and real, but only a jerk cap
    // tight enough to stall the unwind actually opens it.
    //
    // With this clamp in place the sweep re-verifies at 0/112 cells over the
    // envelope, max ratio 1.000000, at every steer_jerk_max tested — including
    // the 3.5781x case above.
    //
    // Re-clamping here costs nothing when the shaped command is already legal
    // (delta_after_lat_yaw is inside this bound by construction, so the clamp
    // only bites on an excursion), and it keeps the smooth manual->AUTO_RESUME
    // hand-over intact INSIDE the safe zone. Outside it, the command snaps to
    // the boundary instead of ramping: leaving the vehicle commanded beyond
    // its lateral-acceleration limit for a tenth of a second, to make the
    // return prettier, is not a trade this envelope is allowed to make.
    const double delta_kappa_max = std::atan(kappa_max * wb);
    delta_final = std::clamp(delta_final, -delta_kappa_max, delta_kappa_max);

    const double steer_norm_out = std::clamp(delta_final / msa, -1.0, 1.0);

    // feature:F7 — the curvature this function's OWN OUTPUT implies, derived
    // from steer_norm_out (not delta_final: the +-1 clamp above can still
    // reduce the magnitude, and what leaves this function is the normalized
    // value). Same wheel_base and max_steer_angle the clamp itself used, so a
    // verifier comparing |kappa_out| against kappa_limit is comparing two
    // numbers produced here, with no wheelbase/speed/timing assumption of its
    // own — see AdSteeringEnvelopeSnapshot::kappa_out for why that matters.
    const double kappa_out = std::tan(steer_norm_out * msa) / wb;

    if (out_snapshot)
    {
        out_snapshot->valid                = true;
        out_snapshot->lateral_accel_active = kappa_clipped && lat_binding;
        out_snapshot->yaw_rate_active       = kappa_clipped && yaw_binding;
        out_snapshot->steer_rate_active     = rate_clipped;
        out_snapshot->steer_jerk_active     = jerk_clipped;
        out_snapshot->any_active           = out_snapshot->lateral_accel_active ||
                                             out_snapshot->yaw_rate_active ||
                                             out_snapshot->steer_rate_active ||
                                             out_snapshot->steer_jerk_active;
        out_snapshot->kappa_cmd            = kappa_cmd;
        out_snapshot->kappa_limit          = kappa_max;
        out_snapshot->kappa_out            = kappa_out;
        out_snapshot->steer_norm_in        = steer_norm_cmd;
        out_snapshot->steer_norm_out       = steer_norm_out;
    }

    return steer_norm_out;
}

void UpdateAdSteeringEnvelopeState(AdSteeringEnvelopeState& state, double applied_steer_norm, double dt)
{
    state.prev_steer_rate_norm = (dt > 0.0) ? (applied_steer_norm - state.prev_steer_norm) / dt : 0.0;
    state.prev_steer_norm      = applied_steer_norm;
}

}  // namespace gt_esmini
