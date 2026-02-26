#include <gtest/gtest.h>

#include "gt_esmini/control/realdriver/LonProfilePlanner.hpp"

TEST(LonProfilePlannerModuleTest, ProfileHasConstantLimits)
{
    gt_esmini::LonProfilePlanner planner;
    planner.SetTargetWithDynamics(9.0, 0.01, gt_esmini::SpeedTransitionShape::STEP);
    planner.Advance(0.01, 9.0);
    auto profile = planner.BuildProfile(3.0);

    ASSERT_FALSE(profile.empty());
    for (const auto& p : profile)
    {
        EXPECT_DOUBLE_EQ(p.a_max, 3.0);
        EXPECT_DOUBLE_EQ(p.j_max, 8.0);
    }
}

TEST(LonProfilePlannerModuleTest, ProfileIsMonotonicWhenAccelerating)
{
    gt_esmini::LonProfilePlanner planner;
    planner.SetTargetWithDynamics(11.0, 0.01, gt_esmini::SpeedTransitionShape::STEP);
    planner.Advance(0.01, 11.0);
    auto profile = planner.BuildProfile(1.0);

    for (size_t i = 1; i < profile.size(); ++i)
    {
        EXPECT_GE(profile[i].v_target, profile[i - 1].v_target);
    }
}
