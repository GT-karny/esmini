#include <gtest/gtest.h>

#include "gt_esmini/control/realdriver/LonProfilePlanner.hpp"

TEST(LonProfilePlannerTest, GeneratesExpectedPointCountAndTimeStep)
{
    gt_esmini::LonProfilePlanner planner;
    const auto profile = planner.BuildProfile(5.0, 20.0);

    ASSERT_EQ(profile.size(), 20u);
    EXPECT_NEAR(profile.front().t_offset, 0.0, 1e-9);
    EXPECT_NEAR(profile.back().t_offset, 2.85, 1e-9);

    for (std::size_t i = 1; i < profile.size(); ++i)
    {
        EXPECT_NEAR(profile[i].t_offset - profile[i - 1].t_offset, 0.15, 1e-9);
        EXPECT_GE(profile[i].v_target, profile[i - 1].v_target);
    }
}

TEST(LonProfilePlannerTest, ClampsNegativeSpeedsToZero)
{
    gt_esmini::LonProfilePlanner planner;
    const auto profile = planner.BuildProfile(-2.0, -1.0);

    ASSERT_EQ(profile.size(), 20u);
    for (const auto& point : profile)
    {
        EXPECT_GE(point.v_target, 0.0);
    }
}
