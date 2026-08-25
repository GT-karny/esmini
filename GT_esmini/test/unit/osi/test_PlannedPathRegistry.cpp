// test_PlannedPathRegistry.cpp -- hand-off slot between an on-board planner and the
// OSI future_trajectory publisher (gt_esmini/osi/GT_PlannedPathRegistry.hpp).
//
// The storage lives in GT_OSIReporter_Moving.cpp (compiled into ScenarioEngine, which
// this test target links). What is worth pinning here is not the container but the two
// rejection rules, because both failure modes are SILENT at runtime: a stale or a
// previous-run path still draws a perfectly plausible line.
#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "gt_esmini/control/virtualdriver/PlannedPathBuilder.hpp"
#include "gt_esmini/osi/GT_PlannedPathRegistry.hpp"

using namespace gt_esmini;

namespace
{

constexpr double kHalfPi = 1.5707963267948966;

PlannedPath MakePath(int object_id, double stamp, size_t n = 3)
{
    PlannedPath p;
    p.object_id = object_id;
    p.stamp     = stamp;
    for (size_t i = 0; i < n; ++i)
    {
        PlannedPathPoint pt;
        pt.t = static_cast<double>(i) * 0.1;
        pt.x = static_cast<double>(i);
        pt.y = 0.0;
        pt.v = 10.0;
        p.points.push_back(pt);
    }
    return p;
}

class PlannedPathRegistryTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        PlannedPathRegistry::Instance().Clear();
        PlannedPathRegistry::Instance().SetConsumerActive(false);
    }
    void TearDown() override
    {
        PlannedPathRegistry::Instance().Clear();
        PlannedPathRegistry::Instance().SetConsumerActive(false);
    }
};

TEST_F(PlannedPathRegistryTest, PublishThenGetReturnsSamePoints)
{
    auto& reg = PlannedPathRegistry::Instance();
    reg.Publish(MakePath(0, 5.0));

    const PlannedPath* got = reg.Get(0, 5.0);
    ASSERT_NE(got, nullptr);
    EXPECT_EQ(got->object_id, 0);
    ASSERT_EQ(got->points.size(), 3u);
    EXPECT_DOUBLE_EQ(got->points[2].x, 2.0);
}

TEST_F(PlannedPathRegistryTest, UnknownObjectReturnsNull)
{
    auto& reg = PlannedPathRegistry::Instance();
    reg.Publish(MakePath(0, 5.0));
    EXPECT_EQ(reg.Get(7, 5.0), nullptr);
}

TEST_F(PlannedPathRegistryTest, RepublishReplacesRatherThanAccumulates)
{
    auto& reg = PlannedPathRegistry::Instance();
    reg.Publish(MakePath(0, 5.0, 3));
    reg.Publish(MakePath(0, 5.05, 5));

    const PlannedPath* got = reg.Get(0, 5.05);
    ASSERT_NE(got, nullptr);
    EXPECT_EQ(got->points.size(), 5u);
    EXPECT_DOUBLE_EQ(got->stamp, 5.05);
}

// A path a few frames old is still the current plan; one that stopped being
// refreshed (controller deactivated, VD lost the domain) must NOT keep drawing.
TEST_F(PlannedPathRegistryTest, RecentPathAcceptedStalePathRejected)
{
    auto& reg = PlannedPathRegistry::Instance();
    reg.Publish(MakePath(0, 5.0));

    EXPECT_NE(reg.Get(0, 5.0 + 0.1, 0.25), nullptr);   // within max_age
    EXPECT_EQ(reg.Get(0, 5.0 + 0.5, 0.25), nullptr);   // beyond max_age
}

// Scenario reload: sim time restarts near 0 while the registry still holds the
// previous run's entry. Without the negative-age check the very first frames of run 2
// would report run 1's path -- a line that looks fine and is entirely fiction.
TEST_F(PlannedPathRegistryTest, PathStampedInTheFutureIsRejected)
{
    auto& reg = PlannedPathRegistry::Instance();
    reg.Publish(MakePath(0, 40.0));  // end of the previous run

    EXPECT_EQ(reg.Get(0, 0.05, 0.25), nullptr);
    EXPECT_EQ(reg.Get(0, 0.0, 0.25), nullptr);
}

// Publishers skip the extra route walk unless a consumer says it will read the result,
// so the flag has to survive independently of the path entries.
TEST_F(PlannedPathRegistryTest, ConsumerFlagIsIndependentOfStoredPaths)
{
    auto& reg = PlannedPathRegistry::Instance();
    EXPECT_FALSE(reg.IsConsumerActive());

    reg.SetConsumerActive(true);
    EXPECT_TRUE(reg.IsConsumerActive());

    reg.Publish(MakePath(0, 1.0));
    reg.Clear();
    EXPECT_TRUE(reg.IsConsumerActive());
    EXPECT_EQ(reg.Get(0, 1.0), nullptr);
}

// ===================================================================================
// BuildPlannedPath -- the two rules that decide whether the reported line ends where
// the vehicle ends up. Both are tested in BOTH polarities: a plan that keeps moving
// must come through untouched, a plan that stops must be cut at the stop.
// ===================================================================================

// A straight plan along +x at constant speed, heading 0.
ShortPlannerSnapshot MakePlan(const std::vector<double>& speeds, double dt = 0.1, double step = 1.0)
{
    ShortPlannerSnapshot plan;
    plan.dt    = dt;
    plan.valid = true;
    double x   = 0.0;
    for (size_t i = 0; i < speeds.size(); ++i)
    {
        TrajectoryPoint tp;
        tp.x = x;
        tp.y = 0.0;
        tp.v = speeds[i];
        tp.t = static_cast<double>(i) * dt;
        tp.h = 0.0;
        plan.preview.push_back(tp);
        x += step;
    }
    return plan;
}

// POLARITY A: nothing stops -> every point survives, positions unchanged (cp = 0).
TEST(PlannedPathBuilderTest, MovingPlanPassesThroughUnchanged)
{
    const ShortPlannerSnapshot plan = MakePlan({10.0, 10.0, 10.0, 10.0});
    const PlannedPath          pp   = BuildPlannedPath(0, 1.0, plan, 0.0);

    ASSERT_EQ(pp.points.size(), 4u);
    for (size_t i = 0; i < pp.points.size(); ++i)
    {
        EXPECT_NEAR(pp.points[i].x, static_cast<double>(i), 1e-9) << "point " << i;
        EXPECT_NEAR(pp.points[i].v, 10.0, 1e-9);
    }
}

// POLARITY B: the plan stops at index 2 -> index 2 and everything after it hold the
// pose of index 1 (the last MOVING point), timestamps preserved. Without this the
// preview's min_preview_span floor keeps walking and the reported path runs past the
// stop line.
TEST(PlannedPathBuilderTest, PlanThatStopsIsFrozenAtTheStop)
{
    const ShortPlannerSnapshot plan = MakePlan({10.0, 10.0, 0.0, 0.0, 0.0});
    const PlannedPath          pp   = BuildPlannedPath(0, 1.0, plan, 0.0);

    ASSERT_EQ(pp.points.size(), 5u);          // the time axis is kept intact
    EXPECT_NEAR(pp.points[1].x, 1.0, 1e-9);   // last moving point
    for (size_t i = 2; i < pp.points.size(); ++i)
    {
        EXPECT_NEAR(pp.points[i].x, 1.0, 1e-9) << "point " << i << " walked past the stop";
        EXPECT_NEAR(pp.points[i].v, 0.0, 1e-9);
        EXPECT_NEAR(pp.points[i].t, static_cast<double>(i) * 0.1, 1e-9);
    }
}

// A plan with no motion anywhere collapses onto its first pose rather than reporting
// the ~10 m the preview floor leaves behind for a standing vehicle.
TEST(PlannedPathBuilderTest, PlanWithNoMotionCollapsesToFirstPose)
{
    const ShortPlannerSnapshot plan = MakePlan({0.0, 0.0, 0.0});
    const PlannedPath          pp   = BuildPlannedPath(0, 1.0, plan, 0.0);

    ASSERT_EQ(pp.points.size(), 3u);
    for (const auto& pt : pp.points)
    {
        EXPECT_NEAR(pt.x, 0.0, 1e-9);
    }
}

// RULE 2 polarity: a non-zero control-point offset shifts every point BACK along its
// own heading, so the published path is in the object-origin frame. cp = 0 must leave
// the path alone (covered by MovingPlanPassesThroughUnchanged above).
TEST(PlannedPathBuilderTest, ControlPointOffsetIsRemovedAlongHeading)
{
    const ShortPlannerSnapshot plan = MakePlan({10.0, 10.0, 10.0});
    const PlannedPath          pp   = BuildPlannedPath(0, 1.0, plan, 3.0);

    ASSERT_EQ(pp.points.size(), 3u);
    for (size_t i = 0; i < pp.points.size(); ++i)
    {
        EXPECT_NEAR(pp.points[i].x, static_cast<double>(i) - 3.0, 1e-9) << "point " << i;
        EXPECT_NEAR(pp.points[i].y, 0.0, 1e-9);
    }
}

// Heading is respected, not assumed to be +x: a plan running along +y must shift in -y.
TEST(PlannedPathBuilderTest, ControlPointOffsetFollowsHeadingNotAxis)
{
    ShortPlannerSnapshot plan;
    plan.valid = true;
    for (int i = 0; i < 3; ++i)
    {
        TrajectoryPoint tp;
        tp.x = 0.0;
        tp.y = static_cast<double>(i);
        tp.v = 8.0;
        tp.t = static_cast<double>(i) * 0.1;
        tp.h = kHalfPi;  // heading +y
        plan.preview.push_back(tp);
    }

    const PlannedPath pp = BuildPlannedPath(0, 1.0, plan, 2.0);
    ASSERT_EQ(pp.points.size(), 3u);
    for (size_t i = 0; i < pp.points.size(); ++i)
    {
        EXPECT_NEAR(pp.points[i].x, 0.0, 1e-9);
        EXPECT_NEAR(pp.points[i].y, static_cast<double>(i) - 2.0, 1e-9);
    }
}

// The extension is subject to the same freeze as the preview: a plan that keeps moving
// through the preview but stops inside the extension must be cut there too.
TEST(PlannedPathBuilderTest, FreezeAppliesToTheExtensionAsWell)
{
    ShortPlannerSnapshot plan = MakePlan({10.0, 10.0});
    for (int i = 0; i < 3; ++i)
    {
        TrajectoryPoint tp;
        tp.x = 2.0 + static_cast<double>(i) * 5.0;
        tp.y = 0.0;
        tp.v = (i == 0) ? 5.0 : 0.0;
        tp.t = 3.0 + static_cast<double>(i) * 0.5;
        tp.h = 0.0;
        plan.extension.push_back(tp);
    }

    const PlannedPath pp = BuildPlannedPath(0, 1.0, plan, 0.0);
    ASSERT_EQ(pp.points.size(), 5u);
    EXPECT_NEAR(pp.points[2].x, 2.0, 1e-9);   // last moving point (first extension sample)
    EXPECT_NEAR(pp.points[3].x, 2.0, 1e-9);
    EXPECT_NEAR(pp.points[4].x, 2.0, 1e-9);
}

}  // namespace
