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

}  // namespace gt_esmini
