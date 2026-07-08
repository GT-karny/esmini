#include <gtest/gtest.h>

#include "gt_esmini/control/virtualdriver/PIDPurePursuitDriver.hpp"

using namespace gt_esmini;

namespace
{
// Build a straight preview along +x at constant target speed.
ShortPlannerSnapshot StraightPreview(double v_target, int n = 30, double dt = 0.1, double speed = 10.0)
{
    ShortPlannerSnapshot s;
    s.dt    = dt;
    s.valid = true;
    double x = 0.0;
    for (int i = 0; i < n; ++i)
    {
        s.preview.push_back({x, 0.0, v_target, i * dt});
        x += speed * dt;
    }
    return s;
}
}  // namespace

TEST(PIDPurePursuitDriver, StraightPreviewGivesNearZeroSteering)
{
    PIDPurePursuitDriver drv;
    auto                 plan = StraightPreview(10.0);
    DriverState          st;
    st.x = 0.0; st.y = 0.0; st.h = 0.0; st.speed = 10.0;

    DriverModelSnapshot snap;
    auto                cmd = drv.Compute(plan, st, 0.1, &snap);

    EXPECT_NEAR(cmd.steering, 0.0, 1e-3);
    EXPECT_TRUE(snap.valid);
}

TEST(PIDPurePursuitDriver, PreviewCurvingLeftSteersLeftNegative)
{
    // RealVehicle convention: +steering = right, -steering = left.
    // A preview bending toward +y (left, with heading 0) must steer negative.
    PIDPurePursuitDriver drv;
    ShortPlannerSnapshot plan;
    plan.dt = 0.1; plan.valid = true;
    for (int i = 0; i < 30; ++i)
    {
        double x = i * 1.0;
        double y = 0.02 * x * x;  // curving to the left (+y)
        plan.preview.push_back({x, y, 10.0, i * 0.1});
    }
    DriverState st;
    st.x = 0.0; st.y = 0.0; st.h = 0.0; st.speed = 10.0;

    auto cmd = drv.Compute(plan, st, 0.1, nullptr);
    EXPECT_LT(cmd.steering, -1e-3);
}

TEST(PIDPurePursuitDriver, PreviewCurvingRightSteersRightPositive)
{
    PIDPurePursuitDriver drv;
    ShortPlannerSnapshot plan;
    plan.dt = 0.1; plan.valid = true;
    for (int i = 0; i < 30; ++i)
    {
        double x = i * 1.0;
        double y = -0.02 * x * x;  // curving to the right (-y)
        plan.preview.push_back({x, y, 10.0, i * 0.1});
    }
    DriverState st;
    st.x = 0.0; st.y = 0.0; st.h = 0.0; st.speed = 10.0;

    auto cmd = drv.Compute(plan, st, 0.1, nullptr);
    EXPECT_GT(cmd.steering, 1e-3);
}

TEST(PIDPurePursuitDriver, BelowTargetSpeedAppliesThrottle)
{
    PIDPurePursuitDriver drv;
    auto                 plan = StraightPreview(/*v_target=*/20.0);
    DriverState          st;
    st.x = 0.0; st.y = 0.0; st.h = 0.0; st.speed = 5.0;  // well below target

    auto cmd = drv.Compute(plan, st, 0.1, nullptr);
    EXPECT_GT(cmd.throttle, 0.0);
    EXPECT_DOUBLE_EQ(cmd.brake, 0.0);
}

TEST(PIDPurePursuitDriver, AboveTargetSpeedAppliesBrake)
{
    PIDPurePursuitDriver drv;
    auto                 plan = StraightPreview(/*v_target=*/5.0);
    DriverState          st;
    st.x = 0.0; st.y = 0.0; st.h = 0.0; st.speed = 20.0;  // well above target

    auto cmd = drv.Compute(plan, st, 0.1, nullptr);
    EXPECT_GT(cmd.brake, 0.0);
    EXPECT_DOUBLE_EQ(cmd.throttle, 0.0);
}

TEST(PIDPurePursuitDriver, InvalidPlanCoasts)
{
    PIDPurePursuitDriver drv;
    ShortPlannerSnapshot plan;  // valid=false, empty
    DriverState          st;
    st.speed = 10.0;

    DriverModelSnapshot snap;
    auto                cmd = drv.Compute(plan, st, 0.1, &snap);
    EXPECT_DOUBLE_EQ(cmd.throttle, 0.0);
    EXPECT_DOUBLE_EQ(cmd.brake, 0.0);
    EXPECT_DOUBLE_EQ(cmd.steering, 0.0);
    EXPECT_FALSE(snap.valid);
}
