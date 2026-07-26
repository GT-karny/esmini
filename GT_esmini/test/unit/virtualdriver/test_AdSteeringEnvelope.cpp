// feature:F7 unit tests for the pure AD steering safety envelope.
//
// Background (see AdSteeringEnvelope.hpp): PIDPurePursuitDriver is stateless
// and has no lateral-deviation/rate/amplitude limit of its own beyond the
// final clamp(+-1); TrajectoryShortPlanner snaps to the lane center every
// frame with no ramp. On a manual->AUTO_RESUME transition after drifting off
// the route, the raw cross-track error turns directly into a maximal
// steering command (measured steer_peak 0.957, yaw rate 1.95 rad/s, FFB
// fully OFF). This module clamps the AD command to four physical limits so
// that can't happen, independent of manual input.

#include <gtest/gtest.h>

#include "gt_esmini/control/virtualdriver/AdSteeringEnvelope.hpp"

#include <cmath>

namespace gt_esmini
{
namespace
{

// Representative vehicle geometry, matching PIDPurePursuitConfig's own
// defaults (PIDPurePursuitDriver.hpp) so the conversion math lines up with
// what the real driver model produces.
constexpr double kWheelBase     = 2.7;   // [m]
constexpr double kMaxSteerAngle = 0.61;  // [rad]

AdSteeringEnvelopeConfig MakeLooseCfg()
{
    // All four limits effectively "off" (huge) so a test can isolate one
    // constraint by tightening only that field. steer_jerk_max must be
    // loosened here too (feature:F7 steering-jerk stage) — it is ON by
    // default (kAdEnvelopeDefaultSteerJerkMax), so a test built on this
    // helper that tightens only e.g. yaw_rate_max would otherwise have its
    // output ALSO narrowed by the still-default-active jerk stage.
    AdSteeringEnvelopeConfig c;
    c.enabled         = true;
    c.a_lat_max_steer = 1000.0;
    c.yaw_rate_max    = 1000.0;
    c.steer_rate_max  = 1000.0;
    c.v_floor         = 0.5;
    c.steer_jerk_max  = 1000.0;
    return c;
}

}  // namespace

// --- Disabled: bit-identical no-op -----------------------------------------

TEST(AdSteeringEnvelopeTest, DisabledIsBitIdenticalNoOp)
{
    AdSteeringEnvelopeState  state;
    AdSteeringEnvelopeConfig cfg = MakeLooseCfg();
    cfg.enabled = false;

    AdSteeringEnvelopeSnapshot snap;
    const double steer_in = 0.6321;  // arbitrary, unremarkable value
    const double out = ComputeAdSteeringEnvelope(steer_in, /*v=*/30.0, kWheelBase, kMaxSteerAngle,
                                                  /*dt=*/0.02, state, cfg, &snap);

    EXPECT_EQ(out, steer_in);       // bit-identical, not just numerically close
    EXPECT_FALSE(snap.valid);
    EXPECT_FALSE(snap.any_active);
}

TEST(AdSteeringEnvelopeTest, DisabledIgnoresNullSnapshot)
{
    AdSteeringEnvelopeState  state;
    AdSteeringEnvelopeConfig cfg = MakeLooseCfg();
    cfg.enabled = false;
    EXPECT_EQ(ComputeAdSteeringEnvelope(0.5, 10.0, kWheelBase, kMaxSteerAngle, 0.02, state, cfg, nullptr), 0.5);
}

// Regression pin: telemetry (ControllerVirtualDriver's envelope.steer_in /
// envelope.steer_out) reads the snapshot unconditionally, disabled or not.
// A prior version of this function early-returned on the disabled path
// without touching *out_snapshot at all, which left steer_in/steer_out at
// their zero-initialized default — misreadable as "the envelope clipped
// this to zero" instead of "the envelope is off". DisabledIsBitIdenticalNoOp
// above only ever checked the RETURN value, so it did not catch this; this
// is exactly the gap that let it through.
TEST(AdSteeringEnvelopeTest, DisabledSnapshotEchoesInputOnBothInAndOut)
{
    AdSteeringEnvelopeState  state;
    AdSteeringEnvelopeConfig cfg = MakeLooseCfg();
    cfg.enabled = false;

    AdSteeringEnvelopeSnapshot snap;
    const double steer_in = -0.417;  // arbitrary non-zero value
    ComputeAdSteeringEnvelope(steer_in, /*v=*/12.0, kWheelBase, kMaxSteerAngle,
                              /*dt=*/0.02, state, cfg, &snap);

    EXPECT_EQ(snap.steer_norm_in, steer_in);
    EXPECT_EQ(snap.steer_norm_out, steer_in);
}

// --- Each constraint clips alone --------------------------------------------

TEST(AdSteeringEnvelopeTest, LateralAccelAloneClipsAtHighSpeed)
{
    AdSteeringEnvelopeState  state;  // prev=0, irrelevant: steer_rate_max is loose
    AdSteeringEnvelopeConfig cfg = MakeLooseCfg();
    cfg.a_lat_max_steer = 2.0;  // only this one is tight

    AdSteeringEnvelopeSnapshot snap;
    const double out = ComputeAdSteeringEnvelope(/*steer_norm_cmd=*/1.0, /*v=*/30.0, kWheelBase,
                                                  kMaxSteerAngle, /*dt=*/0.02, state, cfg, &snap);

    EXPECT_TRUE(snap.valid);
    EXPECT_TRUE(snap.lateral_accel_active);
    EXPECT_FALSE(snap.yaw_rate_active);
    EXPECT_FALSE(snap.steer_rate_active);
    EXPECT_LT(std::fabs(out), 1.0);

    // kappa_max = a_lat_max_steer / v^2; delta = atan(kappa_max * wheel_base).
    const double kappa_max = cfg.a_lat_max_steer / (30.0 * 30.0);
    const double expected  = std::atan(kappa_max * kWheelBase) / kMaxSteerAngle;
    EXPECT_NEAR(out, expected, 1e-9);
}

TEST(AdSteeringEnvelopeTest, YawRateAloneClipsAtModerateSpeed)
{
    AdSteeringEnvelopeState  state;
    AdSteeringEnvelopeConfig cfg = MakeLooseCfg();
    cfg.yaw_rate_max = 0.3;  // only this one is tight

    AdSteeringEnvelopeSnapshot snap;
    const double out = ComputeAdSteeringEnvelope(/*steer_norm_cmd=*/1.0, /*v=*/10.0, kWheelBase,
                                                  kMaxSteerAngle, /*dt=*/0.02, state, cfg, &snap);

    EXPECT_TRUE(snap.yaw_rate_active);
    EXPECT_FALSE(snap.lateral_accel_active);
    EXPECT_FALSE(snap.steer_rate_active);

    const double kappa_max = cfg.yaw_rate_max / 10.0;
    const double expected  = std::atan(kappa_max * kWheelBase) / kMaxSteerAngle;
    EXPECT_NEAR(out, expected, 1e-9);
}

TEST(AdSteeringEnvelopeTest, SteerRateAloneClipsFullLockStep)
{
    AdSteeringEnvelopeState  state;  // prev_steer_norm = 0
    AdSteeringEnvelopeConfig cfg = MakeLooseCfg();
    cfg.steer_rate_max = 0.5;  // [rad/s], only this one is tight

    AdSteeringEnvelopeSnapshot snap;
    const double dt = 0.02;
    const double out = ComputeAdSteeringEnvelope(/*steer_norm_cmd=*/1.0, /*v=*/15.0, kWheelBase,
                                                  kMaxSteerAngle, dt, state, cfg, &snap);

    EXPECT_TRUE(snap.steer_rate_active);
    EXPECT_FALSE(snap.lateral_accel_active);
    EXPECT_FALSE(snap.yaw_rate_active);

    const double max_step = cfg.steer_rate_max * dt;  // 0.01 rad
    EXPECT_NEAR(out, max_step / kMaxSteerAngle, 1e-9);
}

// --- Steering-rate limiter ramps continuously from prev ---------------------

TEST(AdSteeringEnvelopeTest, SteerRateLimiterRampsContinuouslyFromPrev)
{
    AdSteeringEnvelopeState  state;
    state.prev_steer_norm = 0.3;
    AdSteeringEnvelopeConfig cfg = MakeLooseCfg();
    cfg.steer_rate_max = 0.5;  // [rad/s]

    AdSteeringEnvelopeSnapshot snap;
    const double dt = 0.02;
    const double out = ComputeAdSteeringEnvelope(/*steer_norm_cmd=*/1.0, /*v=*/15.0, kWheelBase,
                                                  kMaxSteerAngle, dt, state, cfg, &snap);

    EXPECT_TRUE(snap.steer_rate_active);
    const double delta_prev = state.prev_steer_norm * kMaxSteerAngle;
    const double max_step   = cfg.steer_rate_max * dt;
    const double expected   = (delta_prev + max_step) / kMaxSteerAngle;
    EXPECT_NEAR(out, expected, 1e-9);
    // Continuity: the output must sit within one rate-step of prev, not jump.
    EXPECT_NEAR(out, state.prev_steer_norm + max_step / kMaxSteerAngle, 1e-9);
}

TEST(AdSteeringEnvelopeTest, SteerRateLimiterAlsoBoundsNegativeDirection)
{
    AdSteeringEnvelopeState  state;
    state.prev_steer_norm = -0.3;
    AdSteeringEnvelopeConfig cfg = MakeLooseCfg();
    cfg.steer_rate_max = 0.5;

    AdSteeringEnvelopeSnapshot snap;
    const double dt = 0.02;
    const double out = ComputeAdSteeringEnvelope(/*steer_norm_cmd=*/-1.0, /*v=*/15.0, kWheelBase,
                                                  kMaxSteerAngle, dt, state, cfg, &snap);

    EXPECT_TRUE(snap.steer_rate_active);
    EXPECT_LT(out, state.prev_steer_norm);  // still moved toward the (negative) command
    EXPECT_GT(out, -1.0);                   // but not all the way there in one step
}

// --- Low speed effectively disables the lateral-accel/yaw-rate caps --------

TEST(AdSteeringEnvelopeTest, LowSpeedDefaultLimitsDoNotClipFullLock)
{
    AdSteeringEnvelopeState state;
    state.prev_steer_norm = 1.0;  // matches the command so the rate limiter can't be the cause
    AdSteeringEnvelopeConfig cfg;  // shipped provisional defaults

    AdSteeringEnvelopeSnapshot snap;
    const double out = ComputeAdSteeringEnvelope(/*steer_norm_cmd=*/1.0, /*v=*/0.0, kWheelBase,
                                                  kMaxSteerAngle, /*dt=*/0.02, state, cfg, &snap);

    EXPECT_FALSE(snap.any_active);
    EXPECT_NEAR(out, 1.0, 1e-9);
}

// --- High speed makes lateral accel the dominant cap (default ratio) -------

TEST(AdSteeringEnvelopeTest, HighSpeedDefaultLimitsClipViaLateralAccel)
{
    AdSteeringEnvelopeState state;  // prev=0; steer_rate irrelevant (loosened below)
    AdSteeringEnvelopeConfig cfg;   // shipped provisional defaults
    cfg.steer_rate_max = 1000.0;    // isolate the speed-dependent caps from the rate limiter

    AdSteeringEnvelopeSnapshot snap;
    const double out = ComputeAdSteeringEnvelope(/*steer_norm_cmd=*/1.0, /*v=*/30.0, kWheelBase,
                                                  kMaxSteerAngle, /*dt=*/0.1, state, cfg, &snap);

    EXPECT_TRUE(snap.lateral_accel_active);
    EXPECT_FALSE(snap.yaw_rate_active);
    EXPECT_LT(std::fabs(out), 1.0);
}

// --- Normal driving is never clipped (real measurement-pool regression pin) -
//
// A 15-scenario real-vehicle measurement pool found normal-driving peaks of
// a_lat=3.289 m/s^2, yaw_rate=0.780 rad/s, steer_rate=0.769 rad/s. The
// shipped limits are each that pool max x1.3, so this exact triplet must
// clear all three with margin — the regression pin for "normal driving never
// trips the envelope".

TEST(AdSteeringEnvelopeTest, MeasuredNormalDrivingPoolNeverClips)
{
    constexpr double kMeasuredALat      = 3.289;  // [m/s^2]
    constexpr double kMeasuredYawRate   = 0.780;  // [rad/s]
    constexpr double kMeasuredSteerRate = 0.769;  // [rad/s]

    AdSteeringEnvelopeConfig cfg;  // shipped defaults (4.3 / 1.0 / 1.0 / 1.0)

    // a_lat = kappa*v^2 and yaw_rate = kappa*v must describe the SAME turn:
    // v = a_lat / yaw_rate makes both hold simultaneously.
    const double v         = kMeasuredALat / kMeasuredYawRate;
    const double kappa_cmd = kMeasuredYawRate / v;
    const double delta_cmd = std::atan(kappa_cmd * kWheelBase);
    const double steer_norm_cmd = delta_cmd / kMaxSteerAngle;

    const double dt = 0.02;
    const double delta_rate_step = kMeasuredSteerRate * dt;
    AdSteeringEnvelopeState state;
    state.prev_steer_norm = (delta_cmd - delta_rate_step) / kMaxSteerAngle;
    // feature:F7 steering-jerk stage: this frame represents a STEADY turn
    // already ramped to kMeasuredSteerRate (not a sudden onset from rest), so
    // the realized-rate anchor must reflect that same rate — otherwise the
    // jerk stage would see a phantom 0->kMeasuredSteerRate jump in one frame
    // and spuriously clip a pin that is supposed to never clip.
    state.prev_steer_rate_norm = kMeasuredSteerRate / kMaxSteerAngle;

    AdSteeringEnvelopeSnapshot snap;
    const double out = ComputeAdSteeringEnvelope(steer_norm_cmd, v, kWheelBase, kMaxSteerAngle,
                                                  dt, state, cfg, &snap);

    EXPECT_FALSE(snap.any_active) << "kappa_cmd=" << snap.kappa_cmd << " kappa_limit=" << snap.kappa_limit;
    EXPECT_NEAR(out, steer_norm_cmd, 1e-6);
}

// A wider 27-scenario / 19,557-frame pass surfaced a near-miss: scenario
// 07_oncoming_yield__p017 (v=1.47 m/s oncoming-yield creep, ordinary PID
// micro-oscillation, not a bug) reached steer_rate=0.964 rad/s — 96.4% of the
// OLD 1.0 rad/s cap. This is the reason steer_rate_max was raised to 1.5
// (see AdSteeringEnvelope.hpp for the full rationale: a flat rate cap is
// physically mismatched — lateral jerk scales with speed, so a cap sized for
// highway speed is needlessly tight at creep speed). Real near-miss NORMAL
// case; must never clip.
TEST(AdSteeringEnvelopeTest, MeasuredCreepOncomingYieldSteerRateNeverClips)
{
    constexpr double kMeasuredSteerRate = 0.964;  // [rad/s], near-miss vs the OLD 1.0 cap
    const double v  = 1.47;                        // [m/s] creep speed
    const double dt = 0.02;
    AdSteeringEnvelopeConfig cfg;  // shipped defaults (steer_rate_max=1.5)

    // The pool records rate + speed, not the exact steering magnitude; a
    // small representative creep-steering command is used here. At v=1.47
    // the lateral-accel/yaw-rate caps are wide open (see the low-speed test
    // above), so this isolates the steering-rate check on its own.
    const double steer_norm_cmd  = 0.05;
    const double delta_cmd       = steer_norm_cmd * kMaxSteerAngle;
    const double delta_rate_step = kMeasuredSteerRate * dt;

    AdSteeringEnvelopeState state;
    state.prev_steer_norm = (delta_cmd - delta_rate_step) / kMaxSteerAngle;
    // feature:F7 steering-jerk stage: same reasoning as the pin above — this
    // is a STEADY creep-steer rate, not a sudden onset, so the realized-rate
    // anchor must already reflect kMeasuredSteerRate.
    state.prev_steer_rate_norm = kMeasuredSteerRate / kMaxSteerAngle;

    AdSteeringEnvelopeSnapshot snap;
    const double out = ComputeAdSteeringEnvelope(steer_norm_cmd, v, kWheelBase, kMaxSteerAngle,
                                                  dt, state, cfg, &snap);

    EXPECT_FALSE(snap.any_active) << "kappa_cmd=" << snap.kappa_cmd << " kappa_limit=" << snap.kappa_limit;
    EXPECT_NEAR(out, steer_norm_cmd, 1e-6);
}

// --- Measured pathological inputs are reliably clipped to the shipped limit -
//
// Same measurement pool: pathological (defect) frames reached normalized
// steering rate 6.0-6.3 /s (= 3.66-3.84 rad/s of front-wheel angle) and
// commanded lateral accel 11.8-29.1 m/s^2. Both are far above the shipped
// caps and must be brought down to exactly them (a_lat_max_steer=4.3,
// steer_rate_max=1.5 — the expected values below read the cap FROM cfg, so
// they track the shipped default rather than hardcoding it).

TEST(AdSteeringEnvelopeTest, MeasuredPathologicalSteerRateClipsToLimit)
{
    constexpr double kMeasuredNormRate = 6.3;  // [1/s] normalized steer rate, pool max
    const double dt = 0.02;
    const double v  = 10.0;  // moderate speed: isolates the rate limiter (checked below)
    AdSteeringEnvelopeConfig cfg;  // shipped defaults (steer_rate_max=1.5)
    cfg.steer_jerk_max = 1000.0;  // isolate: only the rate limiter (not the newer jerk stage) should fire

    AdSteeringEnvelopeState state;
    state.prev_steer_norm = 0.0;
    const double steer_norm_cmd = kMeasuredNormRate * dt;  // commanded jump this frame

    AdSteeringEnvelopeSnapshot snap;
    const double out = ComputeAdSteeringEnvelope(steer_norm_cmd, v, kWheelBase, kMaxSteerAngle,
                                                  dt, state, cfg, &snap);

    ASSERT_FALSE(snap.lateral_accel_active);  // isolated: only the rate limiter should fire
    ASSERT_FALSE(snap.yaw_rate_active);
    EXPECT_TRUE(snap.steer_rate_active);

    // The clipped delta-rate must land exactly at the configured cap (1.5 rad/s).
    const double applied_delta_rate = (out - state.prev_steer_norm) * kMaxSteerAngle / dt;
    EXPECT_NEAR(applied_delta_rate, cfg.steer_rate_max, 1e-6);
}

TEST(AdSteeringEnvelopeTest, MeasuredPathologicalLateralAccelClipsToLimit)
{
    constexpr double kMeasuredALat = 29.1;  // [m/s^2] pool max
    const double v = 20.0;                  // well above the a_lat/yaw crossover (4.3 m/s)
    AdSteeringEnvelopeConfig cfg;            // shipped defaults
    cfg.steer_rate_max = 1000.0;             // isolate: only the lateral-accel cap should fire
                                             // (NOTE: a naive prev==cmd trick does NOT isolate
                                             // here — the lat clip moves the output far from the
                                             // raw command, which would itself trip a tight rate cap)

    const double kappa_cmd = kMeasuredALat / (v * v);
    const double delta_cmd = std::atan(kappa_cmd * kWheelBase);
    const double steer_norm_cmd = delta_cmd / kMaxSteerAngle;

    AdSteeringEnvelopeState state;  // prev_steer_norm = 0; rate cap loosened above

    AdSteeringEnvelopeSnapshot snap;
    const double out = ComputeAdSteeringEnvelope(steer_norm_cmd, v, kWheelBase, kMaxSteerAngle,
                                                  0.02, state, cfg, &snap);

    EXPECT_TRUE(snap.lateral_accel_active);
    EXPECT_FALSE(snap.yaw_rate_active);
    EXPECT_FALSE(snap.steer_rate_active);
    EXPECT_LT(std::fabs(out), std::fabs(steer_norm_cmd));

    // The clipped curvature must land exactly at a_lat_max_steer / v^2.
    const double applied_a_lat = snap.kappa_limit * v * v;
    EXPECT_NEAR(applied_a_lat, cfg.a_lat_max_steer, 1e-6);
}

// --- Real regression-scenario anomaly: single-frame full-lock spike --------
//
// Found in recorded telemetry (not synthesized): steer_norm swings
// -0.12 -> 1.0 -> -0.20 across three consecutive frames at v=14 m/s. Whatever
// the raw AD command does, the ACTUALLY applied command may never move
// faster than steer_rate_max per frame — the structural invariant this
// module exists to guarantee against exactly this kind of spike.

TEST(AdSteeringEnvelopeTest, MeasuredSingleFrameFullLockSpikeIsSmoothed)
{
    const double v  = 14.0;
    const double dt = 0.02;
    AdSteeringEnvelopeConfig cfg;  // shipped defaults
    const double max_step_norm = (cfg.steer_rate_max * dt) / kMaxSteerAngle;

    AdSteeringEnvelopeState state;
    state.prev_steer_norm = -0.12;  // frame 1 already applied

    AdSteeringEnvelopeSnapshot snap2;
    const double out2 = ComputeAdSteeringEnvelope(/*steer_norm_cmd=*/1.0, v, kWheelBase, kMaxSteerAngle,
                                                   dt, state, cfg, &snap2);
    EXPECT_TRUE(snap2.any_active);
    EXPECT_LT(out2, 1.0);  // did not jump straight to the raw spike
    EXPECT_LE(std::fabs(out2 - state.prev_steer_norm), max_step_norm + 1e-9);

    // Caller persists whatever was actually applied (out2, since this is the
    // AUTO case) as next frame's rate-limit anchor; the raw command then
    // reverses hard the other way.
    state.prev_steer_norm = out2;
    AdSteeringEnvelopeSnapshot snap3;
    const double out3 = ComputeAdSteeringEnvelope(/*steer_norm_cmd=*/-0.20, v, kWheelBase, kMaxSteerAngle,
                                                   dt, state, cfg, &snap3);
    EXPECT_LE(std::fabs(out3 - state.prev_steer_norm), max_step_norm + 1e-9);
}

// --- Pathological input is reliably clipped ---------------------------------

TEST(AdSteeringEnvelopeTest, PathologicalFullLockStepIsClipped)
{
    // Full-lock command from a standstill-ish previous angle, at a realistic
    // driving speed with the shipped provisional defaults — the exact resume
    // scenario this feature exists for.
    AdSteeringEnvelopeState state;
    state.prev_steer_norm = 0.0;
    AdSteeringEnvelopeConfig cfg;  // shipped provisional defaults

    AdSteeringEnvelopeSnapshot snap;
    const double out = ComputeAdSteeringEnvelope(/*steer_norm_cmd=*/1.0, /*v=*/15.0, kWheelBase,
                                                  kMaxSteerAngle, /*dt=*/0.02, state, cfg, &snap);

    EXPECT_TRUE(snap.any_active);
    EXPECT_LT(std::fabs(out), 0.5);  // nowhere near the commanded full lock
    EXPECT_TRUE(std::isfinite(out));
}

// --- Robustness: v=0 must not divide by zero --------------------------------

TEST(AdSteeringEnvelopeTest, ZeroSpeedDoesNotDivideByZero)
{
    AdSteeringEnvelopeState state;
    AdSteeringEnvelopeConfig cfg;  // v_floor > 0 protects the normal path
    AdSteeringEnvelopeSnapshot snap;
    const double out = ComputeAdSteeringEnvelope(1.0, /*v=*/0.0, kWheelBase, kMaxSteerAngle,
                                                  0.02, state, cfg, &snap);
    EXPECT_TRUE(std::isfinite(out));
    EXPECT_TRUE(std::isfinite(snap.kappa_cmd));
    EXPECT_TRUE(std::isfinite(snap.kappa_limit));
}

TEST(AdSteeringEnvelopeTest, ZeroSpeedAndZeroFloorStillFinite)
{
    // Even a misconfigured v_floor<=0 must not produce a divide-by-zero: the
    // function floors v_eff internally regardless of the config value.
    AdSteeringEnvelopeState state;
    AdSteeringEnvelopeConfig cfg;
    cfg.v_floor = 0.0;
    AdSteeringEnvelopeSnapshot snap;
    const double out = ComputeAdSteeringEnvelope(1.0, /*v=*/0.0, kWheelBase, kMaxSteerAngle,
                                                  0.02, state, cfg, &snap);
    EXPECT_TRUE(std::isfinite(out));
    EXPECT_LE(std::fabs(out), 1.0);
}

// --- feature:F7 steering-JERK limit (a further narrowing of the steering-
// rate limiter above) -------------------------------------------------------
//
// See kAdEnvelopeDefaultSteerJerkMax (AdSteeringEnvelope.hpp) for the
// real-log rationale: normal-driving p99 jerk = 2.0 /s^2, shipped cap = 25
// /s^2, both far below the observed pathological max of 290 /s^2.

// Disabled (<=0) must be a BIT-IDENTICAL no-op onto the rate-only limiter
// code path — required for the regression baseline's deviation=0 check.
TEST(AdSteeringEnvelopeTest, SteerJerkDisabledIsBitIdenticalToRateOnlyLimiter)
{
    AdSteeringEnvelopeState  state;
    state.prev_steer_norm = 0.3;
    AdSteeringEnvelopeConfig cfg = MakeLooseCfg();
    cfg.steer_rate_max = 0.5;
    cfg.steer_jerk_max = 0.0;  // disabled

    AdSteeringEnvelopeSnapshot snap;
    const double dt = 0.02;
    const double out = ComputeAdSteeringEnvelope(/*steer_norm_cmd=*/1.0, /*v=*/15.0, kWheelBase,
                                                  kMaxSteerAngle, dt, state, cfg, &snap);

    // Same expectation as SteerRateLimiterRampsContinuouslyFromPrev: the
    // rate-only window, unmodified by the (disabled) jerk stage.
    const double delta_prev = state.prev_steer_norm * kMaxSteerAngle;
    const double max_step   = cfg.steer_rate_max * dt;
    const double expected   = (delta_prev + max_step) / kMaxSteerAngle;
    EXPECT_EQ(out, expected);
    EXPECT_FALSE(snap.steer_jerk_active);
}

TEST(AdSteeringEnvelopeTest, SteerJerkNegativeAlsoDisablesLimiter)
{
    AdSteeringEnvelopeState  state;
    state.prev_steer_norm = 0.3;
    AdSteeringEnvelopeConfig cfg = MakeLooseCfg();
    cfg.steer_rate_max = 0.5;
    cfg.steer_jerk_max = -5.0;  // negative also disables, same as 0.0

    AdSteeringEnvelopeSnapshot snap;
    const double dt = 0.02;
    const double out = ComputeAdSteeringEnvelope(/*steer_norm_cmd=*/1.0, /*v=*/15.0, kWheelBase,
                                                  kMaxSteerAngle, dt, state, cfg, &snap);

    const double delta_prev = state.prev_steer_norm * kMaxSteerAngle;
    const double max_step   = cfg.steer_rate_max * dt;
    const double expected   = (delta_prev + max_step) / kMaxSteerAngle;
    EXPECT_EQ(out, expected);
    EXPECT_FALSE(snap.steer_jerk_active);
}

// From rest (prev rate 0), a step command's first-frame displacement is
// bounded by the jerk window (steer_jerk_max*msa*dt per frame of allowed
// rate change, times dt again for the resulting position change), not by the
// (looser here) rate limiter.
TEST(AdSteeringEnvelopeTest, SteerJerkStepFromRestBoundsFirstFrameDisplacement)
{
    AdSteeringEnvelopeState  state;  // prev_steer_norm=0, prev_steer_rate_norm=0 (from rest)
    AdSteeringEnvelopeConfig cfg = MakeLooseCfg();
    // The CANDIDATE value, not the shipped default — the shipped default is 0
    // (disabled) while the envelope-escape defect is unfixed, so a test that
    // exercises the jerk stage has to ask for the value explicitly.
    cfg.steer_jerk_max = kAdEnvelopeCandidateSteerJerkMax;  // only this one tight (25.0)

    AdSteeringEnvelopeSnapshot snap;
    const double dt = 0.01;
    const double out = ComputeAdSteeringEnvelope(/*steer_norm_cmd=*/1.0, /*v=*/30.0, kWheelBase,
                                                  kMaxSteerAngle, dt, state, cfg, &snap);

    EXPECT_TRUE(snap.steer_jerk_active);
    EXPECT_TRUE(snap.any_active);
    const double max_disp_rad = cfg.steer_jerk_max * kMaxSteerAngle * dt * dt;  // 25*msa*0.01*0.01
    EXPECT_LE(std::fabs(out) * kMaxSteerAngle, max_disp_rad + 1e-12);
}

// Commanding full lock continuously from rest, the realized rate ramps up
// jerk-limited (~steer_jerk_max per second) until it reaches steer_rate_max,
// which with the shipped defaults (1.5 rad/s cap, 25 /s^2 jerk) takes
// steer_rate_max / (steer_jerk_max*max_steer_angle) =
// 1.5 / (25*0.61) ~= 0.098s, i.e. ~10 frames at dt=0.01 — not yet reached by
// frame 9, reached by frame 10-11.
TEST(AdSteeringEnvelopeTest, SteerJerkRampReachesRateCapAroundTenFrames)
{
    AdSteeringEnvelopeState state;  // starts at rest
    AdSteeringEnvelopeConfig cfg;   // shipped: steer_rate_max=1.5, steer_jerk_max=0 (disabled)
    cfg.steer_jerk_max  = kAdEnvelopeCandidateSteerJerkMax;  // 25.0 — the value under test
    cfg.a_lat_max_steer = 1000.0;   // isolate the rate/jerk ramp from the (unrelated) lat/yaw caps
    cfg.yaw_rate_max    = 1000.0;

    const double dt = 0.01;
    const double v  = 30.0;

    double realized_rate_rad = 0.0;
    for (int frame = 1; frame <= 11; ++frame)
    {
        AdSteeringEnvelopeSnapshot snap;
        const double out = ComputeAdSteeringEnvelope(/*steer_norm_cmd=*/1.0, v, kWheelBase, kMaxSteerAngle,
                                                      dt, state, cfg, &snap);
        UpdateAdSteeringEnvelopeState(state, out, dt);
        realized_rate_rad = state.prev_steer_rate_norm * kMaxSteerAngle;

        if (frame == 9)
        {
            EXPECT_LT(realized_rate_rad, cfg.steer_rate_max - 0.05)
                << "frame 9 should not yet have reached steer_rate_max";
        }
    }
    EXPECT_NEAR(realized_rate_rad, cfg.steer_rate_max, 1e-6);  // reached by frame 11
}

// A hard sign reversal must not flip the realized rate instantaneously — the
// rate CHANGE in a single frame is bounded by steer_jerk_max*max_steer_angle*dt.
TEST(AdSteeringEnvelopeTest, SteerJerkSignReversalBoundsRateChangePerFrame)
{
    AdSteeringEnvelopeState  state;
    state.prev_steer_norm      = 0.5;
    state.prev_steer_rate_norm = 1.5 / kMaxSteerAngle;  // realized rate at +steer_rate_max
    AdSteeringEnvelopeConfig cfg;  // shipped: steer_jerk_max=0 (disabled)
    cfg.steer_jerk_max  = kAdEnvelopeCandidateSteerJerkMax;  // 25.0 — the value under test
    cfg.a_lat_max_steer = 1000.0;  // isolate the jerk stage from the (unrelated) lat/yaw caps
    cfg.yaw_rate_max    = 1000.0;

    const double dt = 0.01;
    AdSteeringEnvelopeSnapshot snap;
    const double out = ComputeAdSteeringEnvelope(/*steer_norm_cmd=*/-1.0, /*v=*/30.0, kWheelBase,
                                                  kMaxSteerAngle, dt, state, cfg, &snap);
    UpdateAdSteeringEnvelopeState(state, out, dt);

    const double new_rate_rad    = state.prev_steer_rate_norm * kMaxSteerAngle;
    const double rate_change     = std::fabs(new_rate_rad - 1.5);
    const double max_rate_change = cfg.steer_jerk_max * kMaxSteerAngle * dt;  // steer_jerk_max*msa*dt
    EXPECT_LE(rate_change, max_rate_change + 1e-9);
    EXPECT_GT(new_rate_rad, 0.0);  // did not flip sign in a single frame
}

// Anchor clamp (reason 1 in AdSteeringEnvelope.cpp): a pathological realized
// rate far beyond steer_rate_max must not force the envelope to keep
// running at that rate — the anchor is clamped to +-steer_rate_max before
// the jerk window is built, capping the forced-continuation duration at
// steer_rate_max/steer_jerk_max (~0.098s with the shipped defaults, ~10
// frames at dt=0.01).
TEST(AdSteeringEnvelopeTest, SteerJerkAnchorClampBoundsForcedContinuationDuration)
{
    AdSteeringEnvelopeState  state;
    state.prev_steer_norm      = 0.4;
    state.prev_steer_rate_norm = 100.0;  // pathological realized rate (e.g. a fast manual swing
                                          // right before AUTO_RESUME) — far beyond steer_rate_max
    AdSteeringEnvelopeConfig cfg;  // shipped defaults
    cfg.a_lat_max_steer = 1000.0;  // isolate the jerk/rate stage from the (unrelated) lat/yaw caps
    cfg.yaw_rate_max    = 1000.0;

    const double dt = 0.01;

    // Frame 1: a "hold in place" command (steer_norm_cmd == prev_steer_norm)
    // must not be forced to move by more than one steer_rate_max step this
    // frame, even with prev_steer_rate_norm=100.0.
    AdSteeringEnvelopeSnapshot snap;
    const double out1 = ComputeAdSteeringEnvelope(/*steer_norm_cmd=*/0.4, /*v=*/30.0, kWheelBase,
                                                   kMaxSteerAngle, dt, state, cfg, &snap);
    EXPECT_LE(std::fabs(out1 - 0.4) * kMaxSteerAngle, cfg.steer_rate_max * dt + 1e-9);
    UpdateAdSteeringEnvelopeState(state, out1, dt);

    // Keep commanding "hold" for steer_rate_max/steer_jerk_max ~= 0.098s
    // (~10 frames at dt=0.01) total.
    double out = out1;
    for (int frame = 2; frame <= 10; ++frame)
    {
        AdSteeringEnvelopeSnapshot s;
        out = ComputeAdSteeringEnvelope(/*steer_norm_cmd=*/0.4, /*v=*/30.0, kWheelBase,
                                         kMaxSteerAngle, dt, state, cfg, &s);
        UpdateAdSteeringEnvelopeState(state, out, dt);
    }
    // The realized rate must have decayed away from the pathological anchor —
    // the forced continuation is no longer running at anywhere near the
    // original 100/s (or even steer_rate_max) by this point.
    const double realized_rate_rad = state.prev_steer_rate_norm * kMaxSteerAngle;
    EXPECT_LT(std::fabs(realized_rate_rad), 0.2);
}

// Robustness: across a spread of dt (including 0) and pathological
// prev_steer_rate_norm anchors, the jerk/rate window must never end up
// empty (reason 2 in AdSteeringEnvelope.cpp) — output stays finite and
// within [-1,1] in every combination.
TEST(AdSteeringEnvelopeTest, SteerJerkWindowNeverEmptyAcrossDtAndPrevRateCombinations)
{
    const double dts[]        = {0.0, 0.001, 0.05, 0.2};
    const double prev_rates[] = {-1000.0, 0.0, 1000.0};

    for (double dt : dts)
    {
        for (double prev_rate : prev_rates)
        {
            AdSteeringEnvelopeState  state;
            state.prev_steer_norm      = 0.2;
            state.prev_steer_rate_norm = prev_rate;
            AdSteeringEnvelopeConfig cfg;  // shipped defaults

            AdSteeringEnvelopeSnapshot snap;
            const double out = ComputeAdSteeringEnvelope(/*steer_norm_cmd=*/1.0, /*v=*/15.0, kWheelBase,
                                                          kMaxSteerAngle, dt, state, cfg, &snap);
            EXPECT_TRUE(std::isfinite(out)) << "dt=" << dt << " prev_rate=" << prev_rate;
            EXPECT_LE(std::fabs(out), 1.0) << "dt=" << dt << " prev_rate=" << prev_rate;
        }
    }
}

// Normal driving (p99 commanded-steering jerk = 2.0 /s^2, far below the
// shipped 25 /s^2 cap) must never trip the jerk stage.
TEST(AdSteeringEnvelopeTest, SteerJerkDoesNotBindNormalDrivingAtP99Jerk)
{
    constexpr double kP99Jerk = 2.0;  // [1/s^2] normalized, real-log p99
    const double dt = 0.02;
    AdSteeringEnvelopeConfig cfg;  // shipped defaults (steer_jerk_max=25.0)
    cfg.a_lat_max_steer = 1000.0;  // isolate the jerk stage from the (unrelated) lat/yaw caps
    cfg.yaw_rate_max    = 1000.0;

    AdSteeringEnvelopeState state;  // starts at rest
    double rate_norm = 0.0;
    double pos_norm  = 0.0;

    for (int frame = 0; frame < 30; ++frame)
    {
        rate_norm += kP99Jerk * dt;
        pos_norm  += rate_norm * dt;

        AdSteeringEnvelopeSnapshot snap;
        const double out = ComputeAdSteeringEnvelope(pos_norm, /*v=*/15.0, kWheelBase, kMaxSteerAngle,
                                                      dt, state, cfg, &snap);
        EXPECT_FALSE(snap.steer_jerk_active) << "frame=" << frame;
        UpdateAdSteeringEnvelopeState(state, out, dt);
    }
}

// --- UpdateAdSteeringEnvelopeState helper ------------------------------------

TEST(AdSteeringEnvelopeTest, UpdateAdSteeringEnvelopeStateComputesRealizedRate)
{
    AdSteeringEnvelopeState state;
    state.prev_steer_norm      = 0.2;
    state.prev_steer_rate_norm = 99.0;  // must be overwritten, not accumulated

    UpdateAdSteeringEnvelopeState(state, /*applied_steer_norm=*/0.5, /*dt=*/0.1);

    EXPECT_NEAR(state.prev_steer_rate_norm, (0.5 - 0.2) / 0.1, 1e-12);
    EXPECT_EQ(state.prev_steer_norm, 0.5);
}

TEST(AdSteeringEnvelopeTest, UpdateAdSteeringEnvelopeStateZeroRateOnNonPositiveDt)
{
    AdSteeringEnvelopeState state;
    state.prev_steer_norm      = 0.2;
    state.prev_steer_rate_norm = 99.0;

    UpdateAdSteeringEnvelopeState(state, /*applied_steer_norm=*/0.5, /*dt=*/0.0);
    EXPECT_EQ(state.prev_steer_rate_norm, 0.0);
    EXPECT_EQ(state.prev_steer_norm, 0.5);

    state.prev_steer_rate_norm = 99.0;
    UpdateAdSteeringEnvelopeState(state, /*applied_steer_norm=*/0.7, /*dt=*/-0.01);
    EXPECT_EQ(state.prev_steer_rate_norm, 0.0);
    EXPECT_EQ(state.prev_steer_norm, 0.7);
}

// Regression guard: the shipped default is ON (not accidentally left
// disabled), so a full-lock command from rest is jerk-clipped out of the box.
// The shipped default is DISABLED, and this pins it that way on purpose.
//
// The jerk stage narrows the window after the curvature clamp, so it can hold
// the applied steering outside the envelope's own lateral-accel ceiling: a
// 112-cell resume sweep measured a_lat 4.571 against the 4.3 ceiling at
// steer_jerk_max=25 (5.545 at 10) versus 2.600 with the stage disabled. Until
// the window is made asymmetric — movement TOWARD the curvature-limited value
// always allowed, only movement away from it shaped — shipping this ON would
// let a shaping term push the command out of a safety envelope.
//
// Flip this test (and kAdEnvelopeDefaultSteerJerkMax) together, and only with
// a measurement showing the a_lat ceiling is respected across the sweep.
TEST(AdSteeringEnvelopeTest, ShippedDefaultLeavesSteerJerkLimitDisabled)
{
    AdSteeringEnvelopeState  state;  // starts at rest
    AdSteeringEnvelopeConfig cfg;    // shipped defaults, nothing overridden
    // Loosened so the RATE limiter is what governs the step below; at the
    // shipped lat/yaw caps the curvature bound would be the binding one at
    // this speed and the step would say nothing about the jerk stage.
    cfg.a_lat_max_steer = 1000.0;
    cfg.yaw_rate_max    = 1000.0;

    AdSteeringEnvelopeSnapshot snap;
    const double out = ComputeAdSteeringEnvelope(/*steer_norm_cmd=*/1.0, /*v=*/30.0, kWheelBase,
                                                  kMaxSteerAngle, /*dt=*/0.01, state, cfg, &snap);
    EXPECT_DOUBLE_EQ(AdSteeringEnvelopeConfig{}.steer_jerk_max, 0.0);
    EXPECT_FALSE(snap.steer_jerk_active);

    // With the jerk stage off the rate limiter still governs, so the frame's
    // step is exactly steer_rate_max*dt from the anchor — i.e. disabling the
    // jerk stage is a no-op onto the pre-feature behaviour, not a hole.
    const double max_step_norm = cfg.steer_rate_max * 0.01 / kMaxSteerAngle;
    EXPECT_NEAR(out, state.prev_steer_norm + max_step_norm, 1e-12);
}

// The curvature cap is an INVARIANT on the output, not merely a stage in the
// pipeline. Whatever the previous applied angle was, whatever dt is, and
// whatever shaping the rate/jerk windows apply, the returned command must
// never imply a curvature above kappa_max.
//
// This regression-pins a measured defect: because the rate and jerk windows
// are centred on prev_steer_norm, a prev angle outside the safe zone used to
// leave the output at the window edge — outside the cap — for as long as the
// shaping took to unwind. An 84-cell AUTO_RESUME sweep saw the output's own
// curvature reach 2.15x kappa_max, held for 0.38s, while the envelope
// reported lateral_accel_active on every one of those frames.
TEST(AdSteeringEnvelopeTest, OutputCurvatureNeverExceedsCapFromAnyPriorState)
{
    for (const double v : {0.0, 2.0, 8.0, 14.0, 30.0})
    {
        for (const double dt : {0.001, 0.01, 0.02, 0.05, 0.2})
        {
            for (const double prev : {-1.0, -0.6, -0.05, 0.0, 0.05, 0.6, 1.0})
            {
                for (const double prev_rate : {-100.0, -2.46, 0.0, 2.46, 100.0})
                {
                    for (const double jerk : {0.0, 10.0, 25.0, 1000.0})
                    {
                        for (const double cmd : {-1.0, -0.3, 0.0, 0.3, 1.0})
                        {
                            AdSteeringEnvelopeConfig cfg;  // shipped limits
                            cfg.steer_jerk_max = jerk;
                            AdSteeringEnvelopeState state;
                            state.prev_steer_norm      = prev;
                            state.prev_steer_rate_norm = prev_rate;

                            AdSteeringEnvelopeSnapshot snap;
                            const double out = ComputeAdSteeringEnvelope(
                                cmd, v, kWheelBase, kMaxSteerAngle, dt, state, cfg, &snap);

                            ASSERT_TRUE(std::isfinite(out));
                            ASSERT_LE(std::fabs(out), 1.0);

                            // Recompute the cap exactly as the implementation does.
                            const double v_eff = std::max({v, cfg.v_floor, 1.0e-6});
                            const double kappa_max = std::min(cfg.a_lat_max_steer / (v_eff * v_eff),
                                                              cfg.yaw_rate_max / v_eff);
                            const double kappa_out =
                                std::fabs(std::tan(out * kMaxSteerAngle) / kWheelBase);

                            // 1e-12 absorbs the atan/tan round trip only.
                            EXPECT_LE(kappa_out, kappa_max + 1e-12)
                                << "v=" << v << " dt=" << dt << " prev=" << prev
                                << " prev_rate=" << prev_rate << " jerk=" << jerk
                                << " cmd=" << cmd << " -> out=" << out;
                        }
                    }
                }
            }
        }
    }
}

}  // namespace gt_esmini
