// req-vd-ad:REQ-AD-025 / vd-func:FUNC-075
//
// Safety-stage pedal arbitration (design §3-1 stage 3, §3-4 closed-loop
// brake conversion). See the header for the full contract, the ACC/MSL
// phase-C landing note, and the deceleration sign convention.

#include "gt_esmini/control/manualdrive/PedalArbitrator.hpp"

#include <algorithm>

namespace gt_esmini
{

PedalArbitrator::PedalArbitrator(const PedalArbitratorConfig& cfg)
    : cfg_(cfg)
{
}

void PedalArbitrator::Reset()
{
    integral_ = 0.0;
}

PedalArbitrationSnapshot PedalArbitrator::Arbitrate(const PedalArbitrationInput& in, double dt)
{
    PedalArbitrationSnapshot snap;

    if (!in.aeb_requested)
    {
        // Quiet: nothing for the safety stage to do. Reset the integrator so
        // the NEXT engagement (whenever it comes) starts clean rather than
        // inheriting whatever error history happened to be running the last
        // time AEB fired -- see Arbitrate's header doc.
        Reset();
        snap.throttle_out = in.driver_throttle;
        snap.brake_out    = in.driver_brake;
        return snap;
    }

    if (in.kickdown_active)
    {
        // §3-2 real-car-style driver override: skip the safety stage this
        // frame. Same reset discipline as the quiet branch, for the same
        // reason (a suppressed frame is not a "paused" firing episode, it is
        // the end of one -- the next engagement should not inherit it).
        Reset();
        snap.throttle_out   = in.driver_throttle;
        snap.brake_out      = in.driver_brake;
        snap.aeb_suppressed = true;
        return snap;
    }

    // Firing, not suppressed: §3-4 closed-loop conversion.
    //
    // error > 0 means the vehicle is decelerating LESS than AEB requires (or
    // is accelerating outright, if measured_decel_mps2 is negative -- mind
    // the sign convention documented on PedalArbitrationInput/in the header
    // banner). error > 0 must always drive the brake command UP.
    const double error = in.aeb_decel_mps2 - in.measured_decel_mps2;
    const double ff    = std::clamp(in.aeb_decel_mps2 / cfg_.full_brake_decel_mps2, 0.0, 1.0);

    // Anti-windup scheme: CONDITIONAL INTEGRATION (a.k.a. integrator
    // clamping by error sign), not a bound on the integral's magnitude.
    //
    // Evaluate what the (unclamped) command would be using the integrator's
    // value from BEFORE this frame -- i.e. what firing at the CURRENT error
    // on TOP OF the existing integral already produces. If that value is
    // already at/past a bound AND this frame's error is still pushing
    // further past the SAME bound, freeze the integral (do not accumulate
    // this frame's contribution) rather than letting it grow somewhere the
    // clamp will discard anyway.
    //
    // This is deliberately NOT "clamp the integral to some large-but-finite
    // value": a magnitude bound still lets the integral grow every frame
    // while saturated, so when the error reverses sign the command stays
    // pinned at the bound for as many frames as it takes the (still huge)
    // integral to bleed back down through zero. Conditional integration
    // never lets the excess accumulate in the first place, so the moment
    // the error direction reverses, the FIRST frame with the new error
    // already computes off an integral that never grew past what was
    // achievable -- the command drops immediately (pinned by
    // CommandSaturatesAtOneAndIntegratorDoesNotWindUp, which asserts this on
    // the very next frame after a 200-frame saturated stretch).
    const double raw_before           = ff + cfg_.brake_kp * error + cfg_.brake_ki * integral_;
    const bool   pushing_further_high = (raw_before >= 1.0) && (error > 0.0);
    const bool   pushing_further_low  = (raw_before <= 0.0) && (error < 0.0);

    if (dt > 0.0 && !pushing_further_high && !pushing_further_low)
    {
        integral_ += error * dt;
    }
    // dt <= 0: never accumulate, regardless of saturation state (guard
    // against paused-sim / first-frame / caller edge cases -- same
    // discipline as AdSteeringEnvelope's dt handling).

    // ORDERING (invisible from the outside, pinned here explicitly):
    // integrate-THEN-output. `raw` below is recomputed using `integral_`
    // AFTER the update above, so THIS frame's error is folded into THIS
    // frame's own command -- it is not deferred to the next call. One
    // consequence: after N calls with a constant nonzero error (and no
    // saturation in between), the returned command reflects N integration
    // steps, not N-1 (test_PedalArbitrator.cpp's
    // ClosedLoopBrakeCommandRisesAcrossFramesInLinearRegion derives its
    // expected values from exactly this fact:
    // raw(n) = ff + kp*error + ki*(n*error*dt)).
    // On a fresh instance with zero error (FirstEngagedFrameEqualsFeedforwardAlone)
    // this choice is unobservable -- error*dt is 0 either way -- but it stops
    // being a free choice the moment error != 0, so it is recorded here
    // rather than left to be reverse-engineered from test output.
    const double raw            = ff + cfg_.brake_kp * error + cfg_.brake_ki * integral_;
    const double brake_request  = std::clamp(raw, 0.0, 1.0);

    snap.aeb_brake_request     = brake_request;
    snap.aeb_engaged           = true;
    snap.throttle_out          = 0.0;
    // §3-1: the human's own (stronger) brake is never weakened, only topped
    // up to the safety demand.
    snap.brake_out             = std::max(in.driver_brake, brake_request);
    snap.driver_brake_dominant = (in.driver_brake >= brake_request);

    return snap;
}

}  // namespace gt_esmini
