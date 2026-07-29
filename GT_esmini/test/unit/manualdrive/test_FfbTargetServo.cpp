// feature:F7 (F7b) unit tests for the pure FFB target-angle servo.
//
// The servo is the numerical core of SDLFFBSink's target-tracking path.
// SDL is not linkable in headless unit tests, so the pure function is
// factored out here and exercised directly.
//
// Numeric expectations come from the spike (scripts/ffb_spike/README.md §1e/f
// and §3d): Kp=4.0/Kd=0.35, max_force=0.6, sign flip on G29 (positive target →
// negative force), and hard-stop taper begins at |actual| > 0.85.

#include <gtest/gtest.h>

#include "gt_esmini/control/manualdrive/FfbTargetServo.hpp"

#include <cmath>

namespace gt_esmini
{
namespace
{

// Defaults used across tests (spike-calibrated numbers).
// friction_ff is explicitly ZEROED here so the tests below keep exercising the
// pure PID + taper core. The shipped default is 0.15; the feed-forward gets its
// own tests at the bottom of this file, which set it explicitly.
SteerServoConfig MakeCfg()
{
    SteerServoConfig c;
    c.kp = 4.0;
    c.kd = 0.35;
    c.max_force = 0.6;
    c.hard_stop_zone = 0.85;
    c.friction_ff = 0.0;
    return c;
}

// Config with the shipped Coulomb feed-forward enabled.
SteerServoConfig MakeCfgWithFF()
{
    SteerServoConfig c = MakeCfg();
    c.friction_ff     = 0.15;
    c.friction_ff_eps = 0.01;
    return c;
}

// Real-rig constants this feature is calibrated against
// (scripts/ffb_spike/CHARACTERIZATION.md §2): the G29 does not move at all
// below this force, in either direction, at any initial wheel angle.
constexpr double kMeasuredMinBreakaway = 0.170;

} // namespace

// --- Zero error / no motion -------------------------------------------------

TEST(FfbTargetServoTest, ZeroErrorProducesZeroForce)
{
    SteerServoState s;
    const auto cfg = MakeCfg();
    const double u = ComputeSteerServoForce(0.0, 0.0, 0.02, s, cfg);
    EXPECT_NEAR(u, 0.0, 1e-9);
}

// --- Sign convention (spike §1f) --------------------------------------------

TEST(FfbTargetServoTest, PositiveErrorProducesNegativeForce)
{
    // target > actual (need to turn RIGHT further). On G29 positive CONSTANT
    // level pushes LEFT, so the commanded force must be NEGATIVE.
    SteerServoState s;
    const auto cfg = MakeCfg();
    // Small err so we don't hit the max_force clamp and can check the linear P.
    const double u = ComputeSteerServoForce(/*target=*/0.10, /*actual=*/0.00, 0.02, s, cfg);
    EXPECT_LT(u, 0.0);
    EXPECT_NEAR(u, -0.10 * cfg.kp, 1e-9);   // P-only on first call (D primed to 0)
}

TEST(FfbTargetServoTest, NegativeErrorProducesPositiveForce)
{
    SteerServoState s;
    const auto cfg = MakeCfg();
    const double u = ComputeSteerServoForce(/*target=*/-0.10, /*actual=*/0.00, 0.02, s, cfg);
    EXPECT_GT(u, 0.0);
    EXPECT_NEAR(u, +0.10 * cfg.kp, 1e-9);
}

// --- Saturation -------------------------------------------------------------

TEST(FfbTargetServoTest, LargeErrorSaturatesAtMaxForce)
{
    SteerServoState s;
    const auto cfg = MakeCfg();
    // err = +1.0 → raw = -4.0, saturates to -0.6.
    const double u = ComputeSteerServoForce(0.9, -0.1, 0.02, s, cfg);
    EXPECT_NEAR(u, -cfg.max_force, 1e-9);
    EXPECT_LE(std::abs(u), cfg.max_force + 1e-12);
}

// --- Derivative priming (spike §3d, "don't inject a stale D spike") ---------

TEST(FfbTargetServoTest, FirstCallHasNoDerivativeContribution)
{
    // Non-zero error but a fresh state should give u = -Kp*err, no D.
    SteerServoState s;
    const auto cfg = MakeCfg();
    const double err = 0.05;
    const double u = ComputeSteerServoForce(err, 0.0, 0.02, s, cfg);
    EXPECT_NEAR(u, -err * cfg.kp, 1e-9);
    EXPECT_TRUE(s.primed);
    EXPECT_NEAR(s.prev_err, err, 1e-9);
}

TEST(FfbTargetServoTest, SecondCallAddsDerivativeContribution)
{
    SteerServoState s;
    const auto cfg = MakeCfg();
    ComputeSteerServoForce(0.05, 0.0, 0.02, s, cfg);              // primes
    const double u2 = ComputeSteerServoForce(0.10, 0.0, 0.02, s, cfg);
    // err rose from 0.05 → 0.10 in dt=0.02, derr = 2.5.
    // u = -(kp*err + kd*derr) = -(0.4 + 0.875) = -1.275 → saturated to -0.6.
    const double raw = -(cfg.kp * 0.10 + cfg.kd * (0.10 - 0.05) / 0.02);
    const double expected = std::max(-cfg.max_force, std::min(cfg.max_force, raw));
    EXPECT_NEAR(u2, expected, 1e-9);
}

// --- Reset re-primes derivative --------------------------------------------

TEST(FfbTargetServoTest, ResetDropsDerivativeOnNextCall)
{
    SteerServoState s;
    const auto cfg = MakeCfg();
    ComputeSteerServoForce(0.10, 0.0, 0.02, s, cfg);   // primes
    ResetSteerServo(s);
    const double u = ComputeSteerServoForce(0.05, 0.0, 0.02, s, cfg);
    // No D on this call (reset), so u = -Kp*err.
    EXPECT_NEAR(u, -0.05 * cfg.kp, 1e-9);
}

// --- Hard-stop taper (spike §3d) --------------------------------------------

TEST(FfbTargetServoTest, HardStopTaperReducesOutwardForce)
{
    // Wheel near +right hard-stop; a POSITIVE target would command NEGATIVE
    // force pushing further into the +axis (outward). This must be tapered.
    // At |actual|=0.925 the taper factor is (1 - (0.925-0.85)/(1-0.85)) = 0.5.
    // Pre-taper: u = -kp*err = -4.0*(1.0-0.925) = -0.3 (below saturation cap).
    // Expected: -0.3 * 0.5 = -0.15.
    SteerServoState s;
    const auto cfg = MakeCfg();
    const double err_pre  = 1.0 - 0.925;
    const double u_pre    = -cfg.kp * err_pre;                 // -0.3 (not saturated)
    const double expected = 0.5 * u_pre;                        // taper factor 0.5
    const double u = ComputeSteerServoForce(1.0, 0.925, 0.02, s, cfg);
    EXPECT_LT(u, 0.0);
    EXPECT_LT(std::abs(u), std::abs(u_pre));                    // strictly less than pre-taper
    EXPECT_NEAR(u, expected, 1e-6);
}

TEST(FfbTargetServoTest, HardStopTaperZeroAtFullLock)
{
    SteerServoState s;
    const auto cfg = MakeCfg();
    // Wheel exactly at +1.0 hard stop, target further right — force must be 0.
    const double u = ComputeSteerServoForce(1.0, 1.0, 0.02, s, cfg);
    EXPECT_NEAR(u, 0.0, 1e-9);
}

TEST(FfbTargetServoTest, HardStopTaperDoesNotAffectInwardRestoration)
{
    // Wheel at +right stop, target = 0 (need to return to center). Force is
    // POSITIVE (pushes wheel LEFT / toward center) — this is INWARD, must
    // NOT be tapered.
    SteerServoState s;
    const auto cfg = MakeCfg();
    const double u = ComputeSteerServoForce(/*target=*/0.0, /*actual=*/0.95, 0.02, s, cfg);
    // Full P: err = -0.95, u = +0.95 * kp = 3.8, saturated to +0.6. No taper.
    EXPECT_NEAR(u, +cfg.max_force, 1e-9);
}

// --- Sign of taper on the -left side ---------------------------------------

TEST(FfbTargetServoTest, HardStopTaperAppliesSymmetricallyOnLeftSide)
{
    // Wheel near -left hard-stop; target NEGATIVE would command POSITIVE force
    // pushing further into -axis (outward on left). Must taper.
    // Same math as the +right test, mirrored: u_pre = +0.3, taper 0.5 → +0.15.
    SteerServoState s;
    const auto cfg = MakeCfg();
    const double err_pre  = -1.0 - (-0.925);      // -0.075
    const double u_pre    = -cfg.kp * err_pre;    // +0.3
    const double expected = 0.5 * u_pre;          // +0.15
    const double u = ComputeSteerServoForce(-1.0, -0.925, 0.02, s, cfg);
    EXPECT_GT(u, 0.0);
    EXPECT_LT(std::abs(u), std::abs(u_pre));
    EXPECT_NEAR(u, expected, 1e-6);
}

// --- Robustness: small dt clamped ------------------------------------------

TEST(FfbTargetServoTest, TinyDtDoesNotExplodeDerivative)
{
    SteerServoState s;
    const auto cfg = MakeCfg();
    ComputeSteerServoForce(0.05, 0.0, 0.02, s, cfg);
    // If the caller sends dt=0 the D term would blow up (div by 0). The
    // function must clamp internally so u stays finite and bounded by max_force.
    const double u = ComputeSteerServoForce(0.06, 0.0, 0.0, s, cfg);
    EXPECT_TRUE(std::isfinite(u));
    EXPECT_LE(std::abs(u), cfg.max_force + 1e-12);
}

// --- Coulomb friction feed-forward (F7b Day-2) -------------------------------
//
// Why this exists: a pure-P servo can only produce Kp*err of force, so errors
// below F_break/Kp cannot break the wheel's static friction. With the shipped
// Kp=4.0 and the G29's measured breakaway ~0.19 that deadband is 0.047 — larger
// than an entire AD lane change (peak steering command 0.065, 96% of it under
// 0.035). Measured consequence: the wheel reached 28.8% of a commanded lane
// change. See scripts/ffb_spike/CHARACTERIZATION.md §4.

TEST(FfbTargetServoTest, WithoutFeedForwardSmallErrorCannotBreakStaticFriction)
{
    // Documents the defect this feature fixes: a production-scale error
    // produces a force the real wheel provably cannot act on.
    SteerServoState s;
    const auto cfg = MakeCfg();   // friction_ff = 0
    const double u = ComputeSteerServoForce(0.02, 0.0, 0.02, s, cfg);
    EXPECT_NEAR(std::abs(u), cfg.kp * 0.02, 1e-9);
    EXPECT_LT(std::abs(u), kMeasuredMinBreakaway);
}

TEST(FfbTargetServoTest, FeedForwardLiftsSmallErrorAboveBreakaway)
{
    SteerServoState s;
    const auto cfg = MakeCfgWithFF();
    const double u = ComputeSteerServoForce(0.02, 0.0, 0.02, s, cfg);
    // First call → no D term. u = -(Kp*err) - ff*tanh(err/eps)
    const double expected = -(cfg.kp * 0.02) - cfg.friction_ff * std::tanh(0.02 / 0.01);
    EXPECT_NEAR(u, expected, 1e-9);
    EXPECT_GT(std::abs(u), kMeasuredMinBreakaway);
}

TEST(FfbTargetServoTest, FeedForwardCannotMoveTheWheelOnItsOwn)
{
    // SAFETY INVARIANT. The feed-forward is bounded by friction_ff, which is
    // held below the wheel's minimum breakaway force. Even with the P term
    // removed and the error saturating the tanh, the servo must not be able to
    // start the wheel by friction compensation alone.
    SteerServoState s;
    auto cfg = MakeCfgWithFF();
    cfg.kp = 0.0;
    cfg.kd = 0.0;
    const double u = ComputeSteerServoForce(1.0, 0.0, 0.02, s, cfg);
    EXPECT_LE(std::abs(u), cfg.friction_ff + 1e-9);
    EXPECT_LT(std::abs(u), kMeasuredMinBreakaway);
}

TEST(FfbTargetServoTest, FeedForwardVanishesAtZeroError)
{
    // tanh(0) = 0, so a settled servo commands nothing — no buzz, no creep.
    // Matches the measured hold test (axis peak-to-peak 0.0, CHARACTERIZATION §7).
    SteerServoState s;
    const auto cfg = MakeCfgWithFF();
    const double u = ComputeSteerServoForce(0.25, 0.25, 0.02, s, cfg);
    EXPECT_NEAR(u, 0.0, 1e-9);
}

TEST(FfbTargetServoTest, FeedForwardPreservesSignConvention)
{
    SteerServoState s1, s2;
    const auto cfg = MakeCfgWithFF();
    EXPECT_LT(ComputeSteerServoForce(+0.03, 0.0, 0.02, s1, cfg), 0.0);
    EXPECT_GT(ComputeSteerServoForce(-0.03, 0.0, 0.02, s2, cfg), 0.0);
}

TEST(FfbTargetServoTest, FeedForwardRespectsMaxForceClamp)
{
    SteerServoState s;
    const auto cfg = MakeCfgWithFF();
    const double u = ComputeSteerServoForce(0.9, 0.0, 0.02, s, cfg);
    EXPECT_LE(std::abs(u), cfg.max_force + 1e-12);
}

// --- Feedback-only output (torque-proxy margin) ------------------------------
//
// OverrideManager asks "how hard is the driver resisting?". The friction
// feed-forward answers nothing about the driver — it is fixed plant
// compensation present whenever err != 0. Reporting the combined force would
// put a standing ~0.15 bias under the 0.20 detection threshold.

TEST(FfbTargetServoTest, FeedbackOutputExcludesFrictionFeedForward)
{
    SteerServoState s_ff, s_plain;
    const auto cfg_ff    = MakeCfgWithFF();
    const auto cfg_plain = MakeCfg();

    double feedback = 0.0;
    const double u_total = ComputeSteerServoForce(0.03, 0.0, 0.02, s_ff, cfg_ff, &feedback);
    const double u_plain = ComputeSteerServoForce(0.03, 0.0, 0.02, s_plain, cfg_plain);

    EXPECT_NEAR(feedback, u_plain, 1e-9);       // == the servo's own effort
    EXPECT_GT(std::abs(u_total), std::abs(feedback));  // total carries the FF
}

TEST(FfbTargetServoTest, FeedbackOutputKeepsFalsePositiveMarginUnderThreshold)
{
    // Shipped detector threshold is 0.20. A hands-off servo tracking a small
    // AD command must stay well under it on the feedback channel even though
    // the applied force is above breakaway.
    SteerServoState s;
    const auto cfg = MakeCfgWithFF();
    double feedback = 0.0;
    const double u_total = ComputeSteerServoForce(0.02, 0.0, 0.02, s, cfg, &feedback);
    EXPECT_GT(std::abs(u_total), kMeasuredMinBreakaway);  // wheel actually moves
    EXPECT_LT(std::abs(feedback), 0.20);                  // detector stays quiet
}

TEST(FfbTargetServoTest, FeedbackOutputHonoursHardStopTaper)
{
    // Near full lock the stop guard suppresses the applied force; the reported
    // feedback must describe what was delivered, not what was requested.
    SteerServoState s;
    const auto cfg = MakeCfgWithFF();
    double feedback = 0.0;
    // actual near +1.0, target beyond it → force pushes further outward.
    ComputeSteerServoForce(1.0, 0.95, 0.02, s, cfg, &feedback);
    const double taper = 1.0 - (0.95 - cfg.hard_stop_zone) / (1.0 - cfg.hard_stop_zone);
    EXPECT_NEAR(std::abs(feedback), cfg.kp * 0.05 * taper, 1e-9);
}

TEST(FfbTargetServoTest, NullFeedbackPointerIsAccepted)
{
    SteerServoState s;
    const auto cfg = MakeCfgWithFF();
    EXPECT_TRUE(std::isfinite(ComputeSteerServoForce(0.05, 0.0, 0.02, s, cfg, nullptr)));
}

TEST(FfbTargetServoTest, ZeroFrictionFeedForwardReproducesLegacyForce)
{
    // Default-OFF principle: with friction_ff = 0 the servo must be numerically
    // identical to the pre-Day-2 implementation, including the feedback output.
    SteerServoState s_a, s_b;
    const auto cfg = MakeCfg();
    double feedback = 0.0;
    ComputeSteerServoForce(0.05, 0.0, 0.02, s_a, cfg);
    ComputeSteerServoForce(0.05, 0.0, 0.02, s_b, cfg, &feedback);
    const double u_a = ComputeSteerServoForce(0.07, 0.01, 0.02, s_a, cfg);
    const double u_b = ComputeSteerServoForce(0.07, 0.01, 0.02, s_b, cfg, &feedback);
    EXPECT_NEAR(u_a, u_b, 1e-12);
    EXPECT_NEAR(feedback, u_b, 1e-12);
}

}  // namespace gt_esmini
