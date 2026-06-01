#include <gtest/gtest.h>

#include "gt_esmini/control/virtualdriver/AutoIndicatorPolicy.hpp"

using namespace gt_esmini;

TEST(AutoIndicatorPolicy, LeftManeuverTurnsLeftOn)
{
    AutoIndicatorPolicy pol;
    IndicatorContext    ctx;
    ctx.maneuver_dir = +1;  // left

    auto out = pol.Update(ctx, 0.1);
    EXPECT_TRUE(out.left_on);
    EXPECT_FALSE(out.right_on);
}

TEST(AutoIndicatorPolicy, RightManeuverTurnsRightOn)
{
    AutoIndicatorPolicy pol;
    IndicatorContext    ctx;
    ctx.maneuver_dir = -1;  // right

    auto out = pol.Update(ctx, 0.1);
    EXPECT_TRUE(out.right_on);
    EXPECT_FALSE(out.left_on);
}

TEST(AutoIndicatorPolicy, NoManeuverBothOff)
{
    AutoIndicatorPolicy pol;
    IndicatorContext    ctx;
    ctx.maneuver_dir = 0;

    auto out = pol.Update(ctx, 0.1);
    EXPECT_FALSE(out.left_on);
    EXPECT_FALSE(out.right_on);
}

TEST(AutoIndicatorPolicy, ManualOverrideWins)
{
    AutoIndicatorPolicy pol;
    IndicatorContext    ctx;
    ctx.manual_active = true;
    ctx.manual_right  = true;
    ctx.maneuver_dir  = +1;  // auto would say left; manual must win

    auto out = pol.Update(ctx, 0.1);
    EXPECT_TRUE(out.right_on);
    EXPECT_FALSE(out.left_on);
}

TEST(AutoIndicatorPolicy, HoldsMinOnTimeAfterManeuverClears)
{
    AutoIndicatorConfig cfg;
    cfg.min_on_time = 0.3;
    AutoIndicatorPolicy pol(cfg);

    IndicatorContext ctx;
    ctx.maneuver_dir = +1;
    pol.Update(ctx, 0.1);  // latch left

    // maneuver clears; indicator should hold until min_on_time elapses
    ctx.maneuver_dir = 0;
    auto a = pol.Update(ctx, 0.1);  // 0.1 elapsed
    EXPECT_TRUE(a.left_on);
    auto b = pol.Update(ctx, 0.1);  // 0.2 elapsed
    EXPECT_TRUE(b.left_on);
    auto c = pol.Update(ctx, 0.2);  // 0.4 elapsed > 0.3 → off
    EXPECT_FALSE(c.left_on);
}
