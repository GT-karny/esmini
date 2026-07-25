// feature:F7 unit tests for the pure AD steering safety envelope.
//
// Background (see AdSteeringEnvelope.hpp): PIDPurePursuitDriver is stateless
// and has no lateral-deviation/rate/amplitude limit of its own beyond the
// final clamp(+-1); TrajectoryShortPlanner snaps to the lane center every
// frame with no ramp. On a manual->AUTO_RESUME transition after drifting off
// the route, the raw cross-track error turns directly into a maximal
// steering command (measured steer_peak 0.957, yaw rate 1.95 rad/s, FFB
// fully OFF). This module clamps the AD command to three physical limits so
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
    // All three limits effectively "off" (huge) so a test can isolate one
    // constraint by tightening only that field.
    AdSteeringEnvelopeConfig c;
    c.enabled         = true;
    c.a_lat_max_steer = 1000.0;
    c.yaw_rate_max    = 1000.0;
    c.steer_rate_max  = 1000.0;
    c.v_floor         = 0.5;
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

}  // namespace gt_esmini
