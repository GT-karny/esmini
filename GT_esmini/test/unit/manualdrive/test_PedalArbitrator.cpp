// req-vd-ad:REQ-AD-025 / vd-func:FUNC-075
//
// Unit tests for PedalArbitrator's safety (AEB) stage -- the acceptance
// ladder of req-vd-ad:REQ-AD-025 (see requirements_vd_ad.yaml for the
// authoritative acceptance_ladder; this file exercises steps a/b/c/d plus
// the §3-4 closed-loop brake conversion and its sign convention).
//
// PHASE A implements ONLY the safety stage; driver_throttle/driver_brake
// stand in for whatever a future phase-C ACC/MSL stage would have already
// produced (design §3-1, §10).

#include <gtest/gtest.h>

#include "gt_esmini/control/manualdrive/PedalArbitrator.hpp"

#include <algorithm>

namespace gt_esmini
{
namespace
{

PedalArbitrationInput MakeQuietInput()
{
    PedalArbitrationInput in;
    in.driver_throttle = 0.0;
    in.driver_brake     = 0.0;
    in.aeb_requested    = false;
    return in;
}

}  // namespace

// --- 025b (negative): AEB quiet -> pure pass-through ------------------------

TEST(PedalArbitratorTest, Req025bAebQuietDriverPedalsPassThroughByteForByte)
{
    PedalArbitrator arb;
    PedalArbitrationInput in;
    in.driver_throttle     = 0.42;  // non-zero, per the acceptance note
    in.driver_brake         = 0.0;
    in.aeb_requested        = false;
    in.aeb_decel_mps2       = 7.0;   // must be ignored entirely: aeb_requested is false
    in.measured_decel_mps2  = 0.0;

    const auto snap = arb.Arbitrate(in, 0.02);

    EXPECT_EQ(snap.throttle_out, in.driver_throttle);
    EXPECT_EQ(snap.brake_out, in.driver_brake);
    EXPECT_FALSE(snap.aeb_engaged);
    EXPECT_FALSE(snap.aeb_suppressed);
}

// --- 025a: AEB firing --------------------------------------------------------

TEST(PedalArbitratorTest, Req025aAebFiringZerosThrottleAndCommandsBrake)
{
    PedalArbitrator arb;
    PedalArbitrationInput in;
    in.driver_throttle      = 0.30;
    in.driver_brake          = 0.0;
    in.aeb_requested         = true;
    in.aeb_decel_mps2        = 4.0;
    in.measured_decel_mps2   = 0.0;

    const auto snap = arb.Arbitrate(in, 0.02);

    EXPECT_EQ(snap.throttle_out, 0.0);
    EXPECT_GT(snap.brake_out, 0.0);
    EXPECT_TRUE(snap.aeb_engaged);
    EXPECT_FALSE(snap.aeb_suppressed);
}

// --- 025c: a stronger driver brake is not added to ---------------------------

TEST(PedalArbitratorTest, Req025cStrongerDriverBrakeIsNotAddedTo)
{
    PedalArbitrator arb;
    PedalArbitrationInput in;
    in.driver_brake          = 0.9;   // already strong
    in.aeb_requested         = true;
    in.aeb_decel_mps2        = 2.0;   // modest request: ff = 2/8 = 0.25, well under 0.9
    in.measured_decel_mps2   = 0.0;

    const auto snap = arb.Arbitrate(in, 0.02);

    EXPECT_EQ(snap.brake_out, in.driver_brake);
    EXPECT_TRUE(snap.driver_brake_dominant);
}

// --- The arbitrator never WEAKENS a driver brake -----------------------------

TEST(PedalArbitratorTest, NeverWeakensDriverBrakeAcrossSweep)
{
    for (double driver_brake : {0.0, 0.1, 0.3, 0.5, 0.7, 0.9, 1.0})
    {
        // Fresh instance per sample: isolates the composition rule from any
        // PI history so this test is purely about max(driver, request).
        PedalArbitrator arb;
        PedalArbitrationInput in;
        in.driver_brake          = driver_brake;
        in.aeb_requested         = true;
        in.aeb_decel_mps2        = 5.0;
        in.measured_decel_mps2   = 0.0;

        const auto snap = arb.Arbitrate(in, 0.02);
        EXPECT_GE(snap.brake_out, driver_brake) << "driver_brake=" << driver_brake;
    }
}

// --- 025d: kickdown suppresses the safety stage entirely ---------------------

TEST(PedalArbitratorTest, Req025dKickdownSuppressesSafetyStageEntirely)
{
    PedalArbitrator arb;
    PedalArbitrationInput in;
    in.driver_throttle      = 0.8;   // floored
    in.driver_brake          = 0.0;
    in.aeb_requested         = true;
    in.aeb_decel_mps2        = 6.0;  // AEB would otherwise fire hard
    in.measured_decel_mps2   = 0.0;
    in.kickdown_active       = true;

    const auto snap = arb.Arbitrate(in, 0.02);

    EXPECT_EQ(snap.throttle_out, in.driver_throttle);
    EXPECT_EQ(snap.brake_out, in.driver_brake);
    EXPECT_FALSE(snap.aeb_engaged);
    EXPECT_TRUE(snap.aeb_suppressed);
    EXPECT_EQ(snap.aeb_brake_request, 0.0);
}

// --- §3-4: closed-loop brake conversion --------------------------------------

TEST(PedalArbitratorTest, ClosedLoopBrakeCommandRisesAcrossFramesInLinearRegion)
{
    // §3-4's claim under test: while the achieved deceleration falls short
    // of the request, the integral term keeps RAISING the brake command.
    // Saturation at 1.0 is correct terminal behaviour for that claim, not a
    // counterexample to it -- CommandSaturatesAtOneAndIntegratorDoesNotWindUp
    // below already covers the saturated regime on its own terms. This test
    // instead has to observe the loop strictly INSIDE its linear
    // (unsaturated) region, so the stimulus is DERIVED from the header's own
    // (REQUIRES CALIBRATION) gains rather than hardcoded: a hardcoded
    // stimulus (an earlier version of this test used aeb_decel=5.0,
    // measured=1.0 over 5 frames) silently drifted into the saturated
    // regime once checked against the actual shipped gains -- 0.825+0.048n
    // crosses 1.0 between frame 3 and frame 4 with those numbers, so frames
    // 4 and 5 tied at the clamp and the strict-increase assertion failed by
    // construction, not by a code defect. Deriving the stimulus here means
    // a future recalibration of full_brake_decel_mps2/brake_kp/brake_ki
    // keeps this test inside the region it is meant to exercise instead of
    // repeating that failure silently.
    PedalArbitratorConfig cfg;  // header's current defaults, read directly
    PedalArbitrator       arb(cfg);

    constexpr int    kFrames = 5;
    constexpr double kDt     = 0.02;

    // A moderate, physically plausible request (30% of full braking) and a
    // moderate constant shortfall (achieved deceleration 0.5 m/s^2 below the
    // request) -- chosen away from any boundary, not tuned to any test outcome.
    const double aeb_decel = 0.3 * cfg.full_brake_decel_mps2;
    const double ff        = aeb_decel / cfg.full_brake_decel_mps2;  // == 0.3
    const double error     = 0.5;                                    // [m/s^2] constant shortfall
    const double measured  = aeb_decel - error;

    // Unsaturated closed-loop formula (PedalArbitrator.cpp's "integrate-then-
    // output" ordering, see that file's comment): after n Arbitrate() calls
    // with this constant error and no intervening saturation,
    //   raw(n) = ff + kp*error + ki*(n*error*dt).
    // Solve for the largest n that keeps raw(n) under a ceiling with REAL
    // headroom (0.9 -- i.e. at least 0.1 clear of the 1.0 clamp, not just
    // technically under it):
    //   n < (ceiling - ff - kp*error) / (ki*error*dt)
    constexpr double kCeiling = 0.9;
    const double     n_max =
        (kCeiling - ff - cfg.brake_kp * error) / (cfg.brake_ki * error * kDt);

    // Self-check: the window this test actually runs (kFrames) must sit
    // comfortably inside that bound. With the gains this file was written
    // against (full_brake=8.0, kp=0.05, ki=0.6) n_max ~= 95.8, so kFrames=5
    // has roughly 19x headroom. If a future recalibration shrinks n_max
    // below kFrames, THIS assertion fails first with an explicit "re-derive
    // the stimulus" message, instead of the loop assertions below failing
    // confusingly once the window quietly starts touching the clamp.
    ASSERT_GT(n_max, static_cast<double>(kFrames))
        << "stimulus has run out of headroom under the current gains (n_max=" << n_max
        << "); re-derive aeb_decel/error/kFrames above";

    PedalArbitrationInput in;
    in.aeb_requested       = true;
    in.aeb_decel_mps2      = aeb_decel;
    in.measured_decel_mps2 = measured;

    double prev = -1.0;  // brake_out is always >= 0, so this is a safe initial "lower than anything"
    for (int frame = 0; frame < kFrames; ++frame)
    {
        const auto snap = arb.Arbitrate(in, kDt);

        EXPECT_GT(snap.brake_out, prev) << "frame " << frame;
        // No frame in this window may saturate. A saturated frame proves
        // only that the clamp held (that is what
        // CommandSaturatesAtOneAndIntegratorDoesNotWindUp exists to check),
        // not that the closed loop is actively raising the command -- without
        // this pair of checks a future gain change that pushed the window
        // into saturation would silently degrade this test into "the clamp
        // held five times", which is a vacuous pass for the §3-4 claim.
        EXPECT_LT(snap.brake_out, 1.0) << "frame " << frame;
        EXPECT_LT(snap.aeb_brake_request, 1.0) << "frame " << frame;

        prev = snap.brake_out;
    }
}

TEST(PedalArbitratorTest, CommandSaturatesAtOneAndIntegratorDoesNotWindUp)
{
    PedalArbitrator arb;
    PedalArbitrationInput in;
    in.aeb_requested        = true;
    in.aeb_decel_mps2       = 20.0;  // far above full_brake_decel_mps2 -> ff alone saturates
    in.measured_decel_mps2  = 0.0;   // large, persistent error while saturated

    // Long saturated stretch: with a naively-integrating (windup-prone) PI
    // term the integral would grow unboundedly here even though the output
    // is clamped and cannot move any further.
    for (int frame = 0; frame < 200; ++frame)
    {
        const auto snap = arb.Arbitrate(in, 0.02);
        ASSERT_NEAR(snap.brake_out, 1.0, 1e-9) << "frame " << frame;
    }

    // Measured now catches up to (and exceeds) the request. With anti-windup
    // the command must come back down PROMPTLY on this very frame; with a
    // wound-up integral it would stay pinned near 1.0 for many more frames
    // while the accumulated excess bleeds off.
    in.measured_decel_mps2 = 25.0;  // now over-achieving relative to the request
    const auto snap = arb.Arbitrate(in, 0.02);
    EXPECT_LT(snap.brake_out, 1.0);
}

TEST(PedalArbitratorTest, FirstEngagedFrameEqualsFeedforwardAlone)
{
    PedalArbitratorConfig cfg;
    PedalArbitrator       arb(cfg);
    PedalArbitrationInput in;
    in.aeb_requested         = true;
    in.aeb_decel_mps2        = 3.0;
    in.measured_decel_mps2   = 3.0;  // zero PI error, so P and I both contribute nothing

    const auto snap = arb.Arbitrate(in, 0.02);

    const double expected_ff = std::clamp(in.aeb_decel_mps2 / cfg.full_brake_decel_mps2, 0.0, 1.0);
    // With zero present error, a fresh (non-stale) integrator contributes
    // nothing, so the request must equal the bare feedforward exactly.
    EXPECT_NEAR(snap.aeb_brake_request, expected_ff, 1e-9);
}

TEST(PedalArbitratorTest, ReleasingAebAndReEngagingRestartsFromFeedforward)
{
    PedalArbitrator arb;

    // Build up integrator state over a persistent-error firing stretch.
    PedalArbitrationInput firing;
    firing.aeb_requested        = true;
    firing.aeb_decel_mps2       = 4.0;
    firing.measured_decel_mps2  = 1.0;  // persistent error -> integral accumulates
    for (int frame = 0; frame < 20; ++frame)
    {
        arb.Arbitrate(firing, 0.02);
    }

    // Release AEB for one frame (quiet pass-through per contract) -- this
    // must reset the integrator (Reset-on-release).
    arb.Arbitrate(MakeQuietInput(), 0.02);

    // Re-engage with a zero-error setup, same shape as
    // FirstEngagedFrameEqualsFeedforwardAlone above: if the integral had
    // survived the release this would NOT equal the bare feedforward.
    PedalArbitrationInput reengage;
    reengage.aeb_requested       = true;
    reengage.aeb_decel_mps2      = 3.0;
    reengage.measured_decel_mps2 = 3.0;  // zero error

    const auto snap = arb.Arbitrate(reengage, 0.02);

    const double expected_ff = 3.0 / PedalArbitratorConfig{}.full_brake_decel_mps2;
    EXPECT_NEAR(snap.aeb_brake_request, expected_ff, 1e-9);
}

TEST(PedalArbitratorTest, NonPositiveDtDoesNotIntegrate)
{
    PedalArbitrator arb;
    PedalArbitrationInput in;
    in.aeb_requested        = true;
    in.aeb_decel_mps2       = 3.0;
    in.measured_decel_mps2  = 0.0;  // large, constant error -- would accumulate if integration ran

    const auto snap1 = arb.Arbitrate(in, 0.0);
    const auto snap2 = arb.Arbitrate(in, -0.01);

    // Same inputs, dt<=0 both times: if the integrator advanced on either
    // call (including via a naive `integral += error * dt` that silently
    // SUBTRACTS on a negative dt) the two results would diverge.
    EXPECT_NEAR(snap1.aeb_brake_request, snap2.aeb_brake_request, 1e-9);
}

// --- Sign convention pin ------------------------------------------------------

TEST(PedalArbitratorTest, SignConventionNegativeMeasuredDecelIncreasesBrakeCommand)
{
    // measured_decel_mps2 is POSITIVE when decelerating (see header). A
    // NEGATIVE value means the vehicle is actually ACCELERATING while AEB
    // wants it to stop -- the PI error (requested - measured) must therefore
    // be LARGER, and the resulting brake command HIGHER, not lower. Getting
    // this backwards is silent (both fields are plain doubles) and is
    // exactly the class of mistake this project has already paid a full
    // investigation for once (see the header's sign-convention block).
    PedalArbitratorConfig cfg;

    PedalArbitrationInput decelerating;
    decelerating.aeb_requested        = true;
    decelerating.aeb_decel_mps2       = 3.0;
    decelerating.measured_decel_mps2  = 1.0;   // slowing down, but short of the request

    PedalArbitrationInput accelerating = decelerating;
    accelerating.measured_decel_mps2  = -1.0;  // actually speeding up

    PedalArbitrator arb_decel(cfg);
    PedalArbitrator arb_accel(cfg);
    const auto snap_decel = arb_decel.Arbitrate(decelerating, 0.02);
    const auto snap_accel = arb_accel.Arbitrate(accelerating, 0.02);

    EXPECT_GT(snap_accel.brake_out, snap_decel.brake_out);
}

}  // namespace gt_esmini
