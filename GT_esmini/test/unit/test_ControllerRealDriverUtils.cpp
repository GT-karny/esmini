#include <gtest/gtest.h>
#include <cmath>
#include "gt_esmini/control/ControllerRealDriverUtils.hpp"

namespace gt_esmini::realdetail
{
TEST(ControllerRealDriverUtilsTest, NormalizeAngleWrapsToPiRange)
{
    EXPECT_NEAR(NormalizeAngle(3.0 * M_PI), M_PI, 1e-9);
    EXPECT_NEAR(NormalizeAngle(-3.0 * M_PI), -M_PI, 1e-9);
    EXPECT_NEAR(NormalizeAngle(M_PI / 2.0), M_PI / 2.0, 1e-12);
}

TEST(ControllerRealDriverUtilsTest, SmootherStepIsClampedAndSmooth)
{
    EXPECT_DOUBLE_EQ(SmootherStep(-1.0), 0.0);
    EXPECT_DOUBLE_EQ(SmootherStep(2.0), 1.0);
    EXPECT_NEAR(SmootherStep(0.5), 0.5, 1e-12);
}

TEST(ControllerRealDriverUtilsTest, MapAdasFunctionNameToIndexIsCaseInsensitive)
{
    EXPECT_EQ(MapAdasFunctionNameToIndex("adaptive_cruise_control"), 8);
    EXPECT_EQ(MapAdasFunctionNameToIndex("SPEED_LIMIT_CONTROL"), 23);
    EXPECT_EQ(MapAdasFunctionNameToIndex("unknown_function"), -1);
}

TEST(ControllerRealDriverUtilsTest, ComputeLaneOffsetTransitionDistanceUsesDynamicsDimension)
{
    using Dim = scenarioengine::OSCPrivateAction::DynamicsDimension;

    EXPECT_DOUBLE_EQ(ComputeLaneOffsetTransitionDistance(Dim::DISTANCE, 3.0, 10.0, 1.0), 5.0);
    EXPECT_DOUBLE_EQ(ComputeLaneOffsetTransitionDistance(Dim::TIME, 2.0, 7.5, 1.0), 15.0);
    EXPECT_DOUBLE_EQ(ComputeLaneOffsetTransitionDistance(Dim::RATE, 2.0, 10.0, 3.0), 15.0);
    EXPECT_DOUBLE_EQ(ComputeLaneOffsetTransitionDistance(Dim::RATE, 0.0, 10.0, 3.0), 300.0);
}
}  // namespace gt_esmini::realdetail
