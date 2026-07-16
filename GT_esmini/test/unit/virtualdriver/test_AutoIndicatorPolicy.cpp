#include <gtest/gtest.h>

#include <cmath>

#include "gt_esmini/control/common/JunctionTurn.hpp"
#include "gt_esmini/control/virtualdriver/AutoIndicatorPolicy.hpp"

using namespace gt_esmini;

// ---------------------------------------------------------------------------------------------
// LaneChangeIndicatorDir -- the maneuver direction that feeds IndicatorContext::maneuver_dir.
//
// Regression: ControllerVirtualDriver::DetectManeuverDir used to derive left/right from the lateral
// offset of a ~3 s preview point expressed in the VEHICLE BODY frame. Mid lane change the ego's own
// yaw dominates that offset and the sign inverts, so the ego indicated RIGHT during a LEFT lane
// change. The direction must come from the action's target lane instead.
// ---------------------------------------------------------------------------------------------

// The old, broken rule, kept here only to pin the failure it produced.
namespace
{
int BodyFramePreviewDir(double ego_h, double ego_x, double ego_y, double preview_x, double preview_y)
{
    const double dx      = preview_x - ego_x;
    const double dy      = preview_y - ego_y;
    const double local_y = -dx * std::sin(ego_h) + dy * std::cos(ego_h);
    if (local_y > 0.5) return +1;
    if (local_y < -0.5) return -1;
    return 0;
}
}  // namespace

// The failure mode: a LEFT lane change on a straight +x road. The ego has yawed left by 0.07 rad and
// the 60 m preview point sits 3.5 m to the left of the ego's current position. In the body frame the
// ego's own yaw contributes -60*sin(0.07) = -4.20 m while the lane displacement contributes only
// +3.5*cos(0.07) = +3.49 m, netting local_y = -0.71 m. The body-frame rule therefore reports RIGHT
// during a LEFT lane change. (Observed in the real scenario as local_y swinging +2.03 -> -3.16.)
TEST(LaneChangeIndicatorDir, BodyFramePreviewInvertsUnderEgoYaw)
{
    const double ego_h = 0.07;  // yawed left
    const double ego_x = 0.0, ego_y = 0.0;
    const double preview_x = 60.0, preview_y = 3.5;  // 60 m ahead, one lane to the left

    EXPECT_EQ(BodyFramePreviewDir(ego_h, ego_x, ego_y, preview_x, preview_y), -1)
        << "pins the defect: the old body-frame rule reports RIGHT during a LEFT lane change";

    // The action-derived rule gets it right: travelling +s, moving from lane -1 to lane -2 is... right;
    // from lane -1 to lane 1 (i.e. increasing id) is left. Here: lane -2 -> lane -1 is a LEFT move.
    EXPECT_EQ(LaneChangeIndicatorDir(-2, -1, true), +1);
}

TEST(LaneChangeIndicatorDir, AlongSHigherLaneIdIsLeft)
{
    // Travelling +s: higher lane id is further left.
    EXPECT_EQ(LaneChangeIndicatorDir(-2, -1, true), +1);  // left
    EXPECT_EQ(LaneChangeIndicatorDir(-1, -2, true), -1);  // right
    EXPECT_EQ(LaneChangeIndicatorDir(1, 2, true), +1);    // left
    EXPECT_EQ(LaneChangeIndicatorDir(2, 1, true), -1);    // right
}

TEST(LaneChangeIndicatorDir, AgainstSDirectionIsMirrored)
{
    // Travelling -s: the road's left is the driver's right.
    EXPECT_EQ(LaneChangeIndicatorDir(-2, -1, false), -1);
    EXPECT_EQ(LaneChangeIndicatorDir(-1, -2, false), +1);
    EXPECT_EQ(LaneChangeIndicatorDir(1, 2, false), -1);
    EXPECT_EQ(LaneChangeIndicatorDir(2, 1, false), +1);
}

TEST(LaneChangeIndicatorDir, CrossingTheCenterlineKeepsTheSign)
{
    // Lane id 0 is skipped by esmini's own relative-target resolution; the direction is unaffected.
    EXPECT_EQ(LaneChangeIndicatorDir(-1, 1, true), +1);
    EXPECT_EQ(LaneChangeIndicatorDir(1, -1, true), -1);
}

TEST(LaneChangeIndicatorDir, SameLaneIsNoManeuver)
{
    EXPECT_EQ(LaneChangeIndicatorDir(-1, -1, true), 0);
    EXPECT_EQ(LaneChangeIndicatorDir(2, 2, false), 0);
}

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
