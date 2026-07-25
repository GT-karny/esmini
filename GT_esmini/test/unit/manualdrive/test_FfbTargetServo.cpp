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
SteerServoConfig MakeCfg()
{
    SteerServoConfig c;
    c.kp = 4.0;
    c.kd = 0.35;
    c.max_force = 0.6;
    c.hard_stop_zone = 0.85;
    return c;
}

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

}  // namespace gt_esmini
