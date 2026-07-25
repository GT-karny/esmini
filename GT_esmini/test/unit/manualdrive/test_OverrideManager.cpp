// feature:F7 unit tests for OverrideManager (auto<->manual bidirectional latch).
//
// Locks the existing AUTO->MANUAL latch behavior, then covers the new
// AUTO_RESUME button-edge path (manual -> auto) and same-frame reentry
// suppression that returns the mode as soon as the input drops.

#include <gtest/gtest.h>

#include "gt_esmini/control/manualdrive/OverrideManager.hpp"
#include "gt_esmini/control/manualdrive/ManualDriveConfig.hpp"
#include "gt_esmini/control/manualdrive/ManualDriveTypes.hpp"
#include "gt_esmini/control/common/VehicleCommand.hpp"

namespace gt_esmini
{
namespace
{

ManualDriveConfig MakeConfig(bool enabled = true,
                             const std::string& lat = "manual",
                             const std::string& lon = "manual")
{
    ManualDriveConfig cfg;
    cfg.override_cfg.enabled             = enabled;
    cfg.override_cfg.button_override     = true;
    cfg.override_cfg.steering_threshold  = 0.05;
    cfg.override_cfg.throttle_threshold  = 0.10;
    cfg.override_cfg.brake_threshold     = 0.10;
    cfg.override_cfg.auto_return_timeout = 0.0;
    cfg.domain.lateral                   = lat;
    cfg.domain.longitudinal              = lon;
    return cfg;
}

InputFrame MakeFrame(double steering = 0.0,
                     double throttle = 0.0,
                     double brake    = 0.0,
                     uint32_t buttons = 0)
{
    InputFrame f;
    PedalSteerCommand ps;
    ps.steering = steering;
    ps.throttle = throttle;
    ps.brake    = brake;
    ps.buttons  = buttons;
    f.pedal_steer = ps;
    f.connected   = true;
    return f;
}

}  // namespace

// --- Existing behavior: AUTO->MANUAL latch on threshold cross ---------------

TEST(OverrideManagerTest, DefaultsToAuto)
{
    OverrideManager m;
    m.Configure(MakeConfig());
    EXPECT_FALSE(m.IsAnyManual());
    EXPECT_FALSE(m.IsLateralManual());
    EXPECT_FALSE(m.IsLongitudinalManual());
    EXPECT_FALSE(m.JustTransitionedToManual());
}

TEST(OverrideManagerTest, SteeringOverThresholdLatchesLateralManual)
{
    OverrideManager m;
    m.Configure(MakeConfig());

    // Above threshold -> latch to MANUAL, transition edge fires.
    m.Update(MakeFrame(0.20), 0.02);
    EXPECT_TRUE(m.IsLateralManual());
    EXPECT_FALSE(m.IsLongitudinalManual());
    EXPECT_TRUE(m.JustTransitionedToManual());

    // Release the wheel — latch holds, edge does not re-fire.
    m.Update(MakeFrame(0.0), 0.02);
    EXPECT_TRUE(m.IsLateralManual());
    EXPECT_FALSE(m.JustTransitionedToManual());
}

TEST(OverrideManagerTest, PedalOverThresholdLatchesLongitudinalOnly)
{
    OverrideManager m;
    m.Configure(MakeConfig());

    m.Update(MakeFrame(0.0, 0.5, 0.0), 0.02);
    EXPECT_FALSE(m.IsLateralManual());
    EXPECT_TRUE(m.IsLongitudinalManual());

    m.Update(MakeFrame(0.0, 0.0, 0.5), 0.02);
    EXPECT_TRUE(m.IsLongitudinalManual());
}

TEST(OverrideManagerTest, OverrideButtonLatchesBothDomains)
{
    OverrideManager m;
    m.Configure(MakeConfig());

    m.Update(MakeFrame(0.0, 0.0, 0.0, ButtonBits::OVERRIDE), 0.02);
    EXPECT_TRUE(m.IsLateralManual());
    EXPECT_TRUE(m.IsLongitudinalManual());
}

TEST(OverrideManagerTest, ScenarioDomainStaysAuto)
{
    OverrideManager m;
    m.Configure(MakeConfig(true, "scenario", "manual"));

    // Steering way above threshold — scenario-locked lateral must ignore it.
    m.Update(MakeFrame(0.9, 0.5, 0.0), 0.02);
    EXPECT_FALSE(m.IsLateralManual());
    EXPECT_TRUE(m.IsLongitudinalManual());
}

TEST(OverrideManagerTest, DisabledOverrideForcesScenarioAuto)
{
    OverrideManager m;
    m.Configure(MakeConfig(false, "manual", "scenario"));

    // Override disabled: configured-manual domain sticks to MANUAL, scenario stays AUTO.
    m.Update(MakeFrame(0.9, 0.5, 0.0), 0.02);
    EXPECT_TRUE(m.IsLateralManual());
    EXPECT_FALSE(m.IsLongitudinalManual());
    EXPECT_FALSE(m.JustTransitionedToManual());
    EXPECT_FALSE(m.JustTransitionedToAuto());
}

// --- New feature:F7 — RESUME (manual -> auto) edge path ---------------------

TEST(OverrideManagerTest, ResumeButtonEdgeReturnsToAutoBothDomains)
{
    OverrideManager m;
    m.Configure(MakeConfig());

    // Latch to MANUAL via steering.
    m.Update(MakeFrame(0.30), 0.02);
    ASSERT_TRUE(m.IsLateralManual());

    // Release wheel, still MANUAL (latched).
    m.Update(MakeFrame(0.0), 0.02);
    ASSERT_TRUE(m.IsLateralManual());
    EXPECT_FALSE(m.JustTransitionedToAuto());

    // Press RESUME — rising edge returns both domains to AUTO, edge fires once.
    m.Update(MakeFrame(0.0, 0.0, 0.0, ButtonBits::AUTO_RESUME), 0.02);
    EXPECT_FALSE(m.IsAnyManual());
    EXPECT_TRUE(m.JustTransitionedToAuto());

    // Hold RESUME — no re-fire of the transition edge.
    m.Update(MakeFrame(0.0, 0.0, 0.0, ButtonBits::AUTO_RESUME), 0.02);
    EXPECT_FALSE(m.IsAnyManual());
    EXPECT_FALSE(m.JustTransitionedToAuto());
}

TEST(OverrideManagerTest, ResumeSuppressesSameFrameReintervention)
{
    OverrideManager m;
    m.Configure(MakeConfig());

    // Latch MANUAL.
    m.Update(MakeFrame(0.30), 0.02);
    ASSERT_TRUE(m.IsLateralManual());

    // Same frame: RESUME pressed AND steering still above threshold.
    // The RESUME edge must win: this frame must land in AUTO, not immediately
    // re-latch to MANUAL. This is the guarantee the smoke script relies on.
    m.Update(MakeFrame(0.30, 0.0, 0.0, ButtonBits::AUTO_RESUME), 0.02);
    EXPECT_FALSE(m.IsAnyManual());
    EXPECT_TRUE(m.JustTransitionedToAuto());

    // Next frame: RESUME released, input still above threshold — allowed to
    // re-latch (this is the "you're still holding the wheel" real-world case).
    m.Update(MakeFrame(0.30), 0.02);
    EXPECT_TRUE(m.IsLateralManual());
    EXPECT_TRUE(m.JustTransitionedToManual());
}

TEST(OverrideManagerTest, ResumeIsNoOpWhenAlreadyAuto)
{
    OverrideManager m;
    m.Configure(MakeConfig());

    // AUTO from the start; pressing RESUME should not fire the transition edge.
    m.Update(MakeFrame(0.0, 0.0, 0.0, ButtonBits::AUTO_RESUME), 0.02);
    EXPECT_FALSE(m.IsAnyManual());
    EXPECT_FALSE(m.JustTransitionedToAuto());
}

TEST(OverrideManagerTest, ResumeRequiresRisingEdge)
{
    OverrideManager m;
    m.Configure(MakeConfig());

    // Latch MANUAL.
    m.Update(MakeFrame(0.30), 0.02);
    ASSERT_TRUE(m.IsLateralManual());

    // If RESUME is already held from before the latch (no rising edge visible to
    // the manager since the previous frame had no button), we still consider
    // this a fresh edge. Model the *held* case: prime with RESUME held while in
    // AUTO, then latch to MANUAL, then keep holding — no rising edge -> no return.
    m.Configure(MakeConfig());
    m.Update(MakeFrame(0.0, 0.0, 0.0, ButtonBits::AUTO_RESUME), 0.02);   // holding, still AUTO
    m.Update(MakeFrame(0.30, 0.0, 0.0, ButtonBits::AUTO_RESUME), 0.02);  // manual input arrives while RESUME still held
    EXPECT_TRUE(m.IsLateralManual());
    EXPECT_FALSE(m.JustTransitionedToAuto());

    // Only when RESUME is released and pressed again does it fire.
    m.Update(MakeFrame(0.30, 0.0, 0.0, 0u), 0.02);  // release RESUME
    m.Update(MakeFrame(0.0, 0.0, 0.0, ButtonBits::AUTO_RESUME), 0.02);  // fresh press
    EXPECT_FALSE(m.IsAnyManual());
    EXPECT_TRUE(m.JustTransitionedToAuto());
}

// --- feature:F7 (F7b) — FFB torque-proxy intervention path -----------------
//
// The FFB target-tracking servo (SDLFFBSink target_track) exposes a "how hard
// is the driver pushing against the servo?" sample every frame. This block
// wires that sample into OverrideManager so a physical steering push against
// AD becomes a latch to MANUAL, subject to a debounce (spike §2d release-
// transient concerns) and the existing scenario / RESUME rules.
//
// Config lives under ffb.target_track (spike §3c) and only takes effect when
// UpdateFfbSample() is called with active=true (the servo is running).

namespace
{

ManualDriveConfig MakeConfigWithFfbThresholds(double force_thr = 0.20,
                                              double dev_thr   = 0.04,
                                              double sustain_s = 0.10)
{
    ManualDriveConfig cfg = MakeConfig();
    cfg.ffb.target_track.override_steer_force_threshold = force_thr;
    cfg.ffb.target_track.override_steer_dev_threshold   = dev_thr;
    cfg.ffb.target_track.override_sustain_time          = sustain_s;
    return cfg;
}

// Fresh AUTO-mode frame with no manual input.
InputFrame QuietFrame() { return MakeFrame(0.0, 0.0, 0.0, 0u); }

}  // namespace

TEST(OverrideManagerTest, FfbSampleInactiveNeverLatches)
{
    OverrideManager m;
    m.Configure(MakeConfigWithFfbThresholds());

    FfbInterventionSample sample;
    sample.active           = false;   // servo off — sample must be ignored
    sample.commanded_force  = 1.0;     // way over threshold
    sample.position_error   = 1.0;     // way over threshold

    for (int i = 0; i < 100; ++i)  // 2 s at 50 Hz — plenty over any sustain
    {
        m.UpdateFfbSample(sample);
        m.Update(QuietFrame(), 0.02);
    }
    EXPECT_FALSE(m.IsLateralManual());
    EXPECT_FALSE(m.JustTransitionedToManual());
}

TEST(OverrideManagerTest, FfbSampleBelowThresholdsNeverLatches)
{
    OverrideManager m;
    m.Configure(MakeConfigWithFfbThresholds(/*force*/0.20, /*dev*/0.04));

    FfbInterventionSample sample;
    sample.active          = true;
    sample.commanded_force = 0.15;   // below 0.20
    sample.position_error  = 0.03;   // below 0.04

    for (int i = 0; i < 100; ++i)
    {
        m.UpdateFfbSample(sample);
        m.Update(QuietFrame(), 0.02);
    }
    EXPECT_FALSE(m.IsLateralManual());
}

TEST(OverrideManagerTest, FfbSampleShortBurstDoesNotLatch)
{
    // Over threshold for less than sustain time — must NOT latch.
    OverrideManager m;
    m.Configure(MakeConfigWithFfbThresholds(0.20, 0.04, /*sustain=*/0.10));

    FfbInterventionSample s;
    s.active = true;
    s.commanded_force = 0.50;   // above force threshold
    s.position_error  = 0.00;

    // 4 frames × 20 ms = 80 ms < 100 ms sustain
    for (int i = 0; i < 4; ++i)
    {
        m.UpdateFfbSample(s);
        m.Update(QuietFrame(), 0.02);
    }
    EXPECT_FALSE(m.IsLateralManual());
}

TEST(OverrideManagerTest, FfbSampleSustainedForceLatchesLateral)
{
    OverrideManager m;
    m.Configure(MakeConfigWithFfbThresholds(0.20, 0.04, 0.10));

    FfbInterventionSample s;
    s.active = true;
    s.commanded_force = 0.50;
    s.position_error  = 0.00;

    // 6 × 20 ms = 120 ms > 100 ms sustain
    for (int i = 0; i < 6; ++i)
    {
        m.UpdateFfbSample(s);
        m.Update(QuietFrame(), 0.02);
    }
    EXPECT_TRUE(m.IsLateralManual());
    EXPECT_FALSE(m.IsLongitudinalManual());  // lateral only — pedal path untouched
}

TEST(OverrideManagerTest, FfbSampleSustainedDevAloneLatches)
{
    // dev channel alone (spike §2e fallback) must also fire.
    OverrideManager m;
    m.Configure(MakeConfigWithFfbThresholds(0.20, 0.04, 0.10));

    FfbInterventionSample s;
    s.active = true;
    s.commanded_force = 0.10;   // below force threshold
    s.position_error  = 0.10;   // above dev threshold

    for (int i = 0; i < 6; ++i)
    {
        m.UpdateFfbSample(s);
        m.Update(QuietFrame(), 0.02);
    }
    EXPECT_TRUE(m.IsLateralManual());
}

TEST(OverrideManagerTest, FfbSustainResetsWhenSignalDrops)
{
    // Signal above threshold, then drops (below threshold), then above again.
    // The sustain timer must reset on the drop; otherwise a series of short
    // pushes would eventually accumulate into a spurious latch.
    OverrideManager m;
    m.Configure(MakeConfigWithFfbThresholds(0.20, 0.04, 0.10));

    FfbInterventionSample hi; hi.active = true; hi.commanded_force = 0.50;
    FfbInterventionSample lo; lo.active = true; lo.commanded_force = 0.05;

    // 3 × 20 ms hi = 60 ms
    for (int i = 0; i < 3; ++i) { m.UpdateFfbSample(hi); m.Update(QuietFrame(), 0.02); }
    // 3 × 20 ms lo — sustain resets
    for (int i = 0; i < 3; ++i) { m.UpdateFfbSample(lo); m.Update(QuietFrame(), 0.02); }
    // 4 × 20 ms hi = 80 ms — still under 100 ms
    for (int i = 0; i < 4; ++i) { m.UpdateFfbSample(hi); m.Update(QuietFrame(), 0.02); }

    EXPECT_FALSE(m.IsLateralManual());
}

TEST(OverrideManagerTest, FfbScenarioLateralIsImmuneToFfbIntervention)
{
    // domain.lateral="scenario" locks lateral to AUTO. FFB thresholds crossed
    // sustainably must NOT latch it to MANUAL.
    OverrideManager m;
    ManualDriveConfig cfg = MakeConfigWithFfbThresholds();
    cfg.domain.lateral = "scenario";
    m.Configure(cfg);

    FfbInterventionSample s;
    s.active = true;
    s.commanded_force = 1.0;
    s.position_error  = 1.0;

    for (int i = 0; i < 50; ++i)  // 1 s well past sustain
    {
        m.UpdateFfbSample(s);
        m.Update(QuietFrame(), 0.02);
    }
    EXPECT_FALSE(m.IsLateralManual());
}

TEST(OverrideManagerTest, FfbLatchHoldsAcrossReleaseTransient)
{
    // Spike §2d: after a firm hold, the PID overshoots for ~400 ms releasing.
    // Once latched, the existing OverrideManager latch model must keep MANUAL
    // regardless of what the FFB sample does — clearing goes through RESUME
    // (below), never through "sample dropped below threshold".
    OverrideManager m;
    m.Configure(MakeConfigWithFfbThresholds(0.20, 0.04, 0.10));

    FfbInterventionSample hi; hi.active = true; hi.commanded_force = 0.50;
    for (int i = 0; i < 6; ++i) { m.UpdateFfbSample(hi); m.Update(QuietFrame(), 0.02); }
    ASSERT_TRUE(m.IsLateralManual());

    // Simulate release: signal goes below threshold. Latch stays.
    FfbInterventionSample lo; lo.active = true; lo.commanded_force = 0.03;
    for (int i = 0; i < 50; ++i) { m.UpdateFfbSample(lo); m.Update(QuietFrame(), 0.02); }
    EXPECT_TRUE(m.IsLateralManual());

    // Simulate the release overshoot: signal spikes back above threshold.
    // Latch still stays (already MANUAL — nothing to change).
    FfbInterventionSample spike; spike.active = true; spike.commanded_force = 0.55;
    for (int i = 0; i < 30; ++i) { m.UpdateFfbSample(spike); m.Update(QuietFrame(), 0.02); }
    EXPECT_TRUE(m.IsLateralManual());
}

TEST(OverrideManagerTest, FfbResumeEdgeWinsOverSustainedFfbSameFrame)
{
    // Sustained FFB (would latch on its own) + a RESUME rising edge on the
    // same frame: RESUME wins. The frame after RESUME releases, FFB is still
    // sustained (state carried over) — that frame IS allowed to re-latch.
    OverrideManager m;
    m.Configure(MakeConfigWithFfbThresholds(0.20, 0.04, 0.10));

    FfbInterventionSample hi; hi.active = true; hi.commanded_force = 0.50;

    // Latch MANUAL via sustained FFB.
    for (int i = 0; i < 6; ++i) { m.UpdateFfbSample(hi); m.Update(QuietFrame(), 0.02); }
    ASSERT_TRUE(m.IsLateralManual());

    // Same frame: RESUME pressed AND FFB still sustained.
    m.UpdateFfbSample(hi);
    m.Update(MakeFrame(0.0, 0.0, 0.0, ButtonBits::AUTO_RESUME), 0.02);
    EXPECT_FALSE(m.IsAnyManual());               // RESUME edge wins
    EXPECT_TRUE(m.JustTransitionedToAuto());
}

}  // namespace gt_esmini
