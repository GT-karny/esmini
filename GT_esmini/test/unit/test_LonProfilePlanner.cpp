#include <gtest/gtest.h>

#include "gt_esmini/control/realdriver/LonProfilePlanner.hpp"

TEST(LonProfilePlannerTest, GeneratesExpectedPointCountAndTimeStep)
{
    gt_esmini::LonProfilePlanner planner;
    // Setup: advance with target=20, then build from current=5
    planner.Advance(0.01, 20.0);
    // After one tiny step the smoothed target is near 0 (default start),
    // so set explicitly for this test:
    planner.SetTargetWithDynamics(20.0, 0.01, gt_esmini::SpeedTransitionShape::STEP);
    planner.Advance(0.01, 20.0);

    const auto profile = planner.BuildProfile(5.0);

    ASSERT_EQ(profile.size(), 20u);
    EXPECT_NEAR(profile.front().t_offset, 0.0, 1e-9);
    EXPECT_NEAR(profile.back().t_offset, 2.85, 1e-9);

    for (std::size_t i = 1; i < profile.size(); ++i)
    {
        EXPECT_NEAR(profile[i].t_offset - profile[i - 1].t_offset, 0.15, 1e-9);
        EXPECT_GE(profile[i].v_target, profile[i - 1].v_target);
    }
}

TEST(LonProfilePlannerTest, SmoothedTargetRampsLinearly)
{
    gt_esmini::LonProfilePlanner planner;
    planner.SetTargetWithDynamics(10.0, 1.0, gt_esmini::SpeedTransitionShape::LINEAR);

    // After 0.5s, smoothed target should be ~5.0 (half way)
    for (int i = 0; i < 50; ++i)
    {
        planner.Advance(0.01, 10.0);
    }
    EXPECT_NEAR(planner.GetSmoothedTarget(), 5.0, 0.1);

    // After another 0.5s, should reach 10.0
    for (int i = 0; i < 50; ++i)
    {
        planner.Advance(0.01, 10.0);
    }
    EXPECT_NEAR(planner.GetSmoothedTarget(), 10.0, 0.1);
}

TEST(LonProfilePlannerTest, StepShapeJumpsImmediately)
{
    gt_esmini::LonProfilePlanner planner;
    planner.SetTargetWithDynamics(15.0, 1.0, gt_esmini::SpeedTransitionShape::STEP);
    planner.Advance(0.01, 15.0);
    EXPECT_NEAR(planner.GetSmoothedTarget(), 15.0, 1e-9);
}
