// feature:F7 unit tests for EngineModel's DISPLAY-vs-PHYSICS RPM split.
//
// WHY THIS FILE EXISTS (a measured defect, not a hypothetical one):
//
// EngineModel overlays an Ornstein-Uhlenbeck "idle jitter" on the engine speed
// so gauges and OSI show the small fluctuations a real engine has at idle. That
// generator is seeded from std::random_device whenever idle_jitter_seed is 0 —
// which is the SHIPPED default in config/real_vehicle_params.json. So the
// jittered value differs between two runs of the same scenario, by design.
//
// EngineModel keeps that value out of its own physics outputs (torque, rev
// limiter and creep all read base_rpm). RealVehicle, however, stored only
// GetRPM() and then used it for the engine-compression-braking term, which put
// a random_device-seeded number directly into the vehicle's longitudinal
// acceleration. The result: two identical runs of the same scenario, in
// separate OS processes, diverged in ego.speed by ~1e-9 and then amplified
// through the AD closed loop to ~1e-4. Reproduced on 3 of 6 VirtualDriver
// scenarios; GT_esmini/test/headless/vd_engine_determinism_probe.py is the
// end-to-end reproduction.
//
// These tests pin the contract AT ITS SOURCE: the jitter must be visible in
// GetRPM() and absent from everything a force calculation is allowed to read.
// They cannot catch a future caller reading GetRPM() for physics again — the
// headless probe is what catches that — but they stop the split itself from
// being quietly removed.

#include <gtest/gtest.h>

#include "gt_esmini/control/manualdrive/EngineModel.hpp"

#include <cmath>

namespace gt_esmini
{
namespace
{

EngineModel::Params JitteryParams(uint32_t seed)
{
    EngineModel::Params p;
    p.idle_jitter.sigma_rpm = 20.0;  // the shipped config value
    p.idle_jitter.tau_s     = 1.2;
    p.idle_jitter.seed      = seed;
    return p;
}

// Idle-ish operating point with the converter UNLOCKED (slip_factor 0), which
// is where the jitter has full weight (EngineModel scales it by 1 - slip).
EngineModel::VehicleContext UnlockedIdle()
{
    return EngineModel::VehicleContext{/*abs_speed_mps=*/0.0, /*slip_factor=*/0.0};
}

}  // namespace

// The whole point of the overlay: the DISPLAYED rpm must actually move, and it
// must differ between two different seeds. Without this, the tests below would
// pass trivially on a jitter that does nothing.
TEST(EngineModelTest, DisplayRpmCarriesJitterAndDiffersBySeed)
{
    EngineModel a, b;
    a.SetParams(JitteryParams(1));
    b.SetParams(JitteryParams(2));

    bool moved_off_base = false;
    bool differed       = false;
    for (int i = 0; i < 200; ++i)
    {
        a.Step(/*throttle=*/0.0, /*target_rpm=*/700.0, /*clutch_locked=*/false, UnlockedIdle(), 0.01);
        b.Step(/*throttle=*/0.0, /*target_rpm=*/700.0, /*clutch_locked=*/false, UnlockedIdle(), 0.01);
        if (std::fabs(a.GetRPM() - a.GetBaseRPM()) > 1e-9) moved_off_base = true;
        if (std::fabs(a.GetRPM() - b.GetRPM()) > 1e-9)     differed       = true;
    }
    EXPECT_TRUE(moved_off_base) << "idle jitter never moved the displayed RPM";
    EXPECT_TRUE(differed) << "two different seeds produced identical display RPM";
}

// ...and the physics-facing outputs must be BIT-IDENTICAL across those same two
// seeds. This is the invariant the defect violated downstream.
TEST(EngineModelTest, PhysicsOutputsAreBitIdenticalAcrossJitterSeeds)
{
    EngineModel a, b;
    a.SetParams(JitteryParams(1));
    b.SetParams(JitteryParams(2));

    for (int i = 0; i < 200; ++i)
    {
        a.Step(/*throttle=*/0.0, /*target_rpm=*/700.0, /*clutch_locked=*/false, UnlockedIdle(), 0.01);
        b.Step(/*throttle=*/0.0, /*target_rpm=*/700.0, /*clutch_locked=*/false, UnlockedIdle(), 0.01);

        // EQ, not NEAR: two seeds must not change these by even one ULP, or the
        // simulation stops being reproducible.
        ASSERT_EQ(a.GetBaseRPM(), b.GetBaseRPM())   << "frame " << i;
        ASSERT_EQ(a.GetTorqueNm(), b.GetTorqueNm()) << "frame " << i;
        ASSERT_EQ(a.IsRevLimited(), b.IsRevLimited()) << "frame " << i;
    }
}

// Same invariant over a driving (not idling) operating point, where the torque
// curve is actually being evaluated rather than sitting on the idle governor.
TEST(EngineModelTest, PhysicsOutputsSeedIndependentUnderThrottle)
{
    EngineModel a, b;
    a.SetParams(JitteryParams(7));
    b.SetParams(JitteryParams(99));

    // slip_factor 0.6: converter mostly locked. This is the band the leaked
    // term was active in (RealVehicle applies engine braking when
    // slip_factor > 0.5), and the jitter weight (1 - slip) is still non-zero
    // here — i.e. exactly the overlap that made the defect observable.
    const EngineModel::VehicleContext vctx{6.0, 0.6};
    for (int i = 0; i < 200; ++i)
    {
        a.Step(0.4, 2500.0, true, vctx, 0.01);
        b.Step(0.4, 2500.0, true, vctx, 0.01);
        ASSERT_EQ(a.GetBaseRPM(), b.GetBaseRPM())   << "frame " << i;
        ASSERT_EQ(a.GetTorqueNm(), b.GetTorqueNm()) << "frame " << i;
    }
    // And the display value did diverge in that same band — otherwise this test
    // would be asserting seed-independence over a silent generator.
    EXPECT_NE(a.GetRPM(), b.GetRPM());
}

// A fixed non-zero seed must make even the DISPLAY value reproducible. This is
// the escape hatch for anyone who needs bit-exact OSI/gauge output, and it is
// what the investigation used to localize the defect (pinning the seed made 3
// divergent scenarios bit-identical), so it needs to keep working.
TEST(EngineModelTest, FixedSeedMakesDisplayRpmReproducible)
{
    EngineModel a, b;
    a.SetParams(JitteryParams(4242));
    b.SetParams(JitteryParams(4242));

    for (int i = 0; i < 200; ++i)
    {
        a.Step(0.0, 700.0, false, UnlockedIdle(), 0.01);
        b.Step(0.0, 700.0, false, UnlockedIdle(), 0.01);
        ASSERT_EQ(a.GetRPM(), b.GetRPM()) << "frame " << i;
    }
}

// sigma_rpm <= 0 disables the overlay outright: display and base must coincide
// exactly, so a deployment that wants no jitter at all gets a hard guarantee
// rather than a very small random number.
TEST(EngineModelTest, ZeroSigmaCollapsesDisplayOntoBase)
{
    EngineModel e;
    EngineModel::Params p = JitteryParams(0);  // seed 0 = random_device
    p.idle_jitter.sigma_rpm = 0.0;
    e.SetParams(p);

    for (int i = 0; i < 100; ++i)
    {
        e.Step(0.0, 700.0, false, UnlockedIdle(), 0.01);
        ASSERT_EQ(e.GetRPM(), e.GetBaseRPM()) << "frame " << i;
    }
}

}  // namespace gt_esmini
