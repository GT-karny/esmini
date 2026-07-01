#include <gtest/gtest.h>

#include "gt_esmini/control/common/JunctionTurn.hpp"
#include "gt_esmini/control/common/TransitionDynamics.hpp"

namespace gt_esmini
{

TEST(TransitionDynamicsHelperTest, EvaluatesKnownShapes)
{
    using Shape = scenarioengine::OSCPrivateAction::DynamicsShape;

    EXPECT_DOUBLE_EQ(EvaluateTransitionShape(Shape::LINEAR, 10.0, 4.0, 0.25), 11.0);
    EXPECT_DOUBLE_EQ(EvaluateTransitionShape(Shape::STEP, 10.0, 4.0, 0.0), 14.0);
    EXPECT_DOUBLE_EQ(EvaluateTransitionShape(Shape::CUBIC, 0.0, 1.0, 0.5), 0.5);
    EXPECT_NEAR(EvaluateTransitionShape(Shape::SINUSOIDAL, 0.0, 2.0, 0.5), 1.0, 1e-12);
}

TEST(TransitionDynamicsHelperTest, ClampsProgress)
{
    using Shape = scenarioengine::OSCPrivateAction::DynamicsShape;

    EXPECT_DOUBLE_EQ(EvaluateTransitionShape(Shape::LINEAR, 3.0, 6.0, -1.0), 3.0);
    EXPECT_DOUBLE_EQ(EvaluateTransitionShape(Shape::LINEAR, 3.0, 6.0, 2.0), 9.0);
}

TEST(JunctionTurnHelperTest, ClassifiesHeadingDelta)
{
    EXPECT_EQ(TurnDirectionFromHeadingDelta(0.20), 1);
    EXPECT_EQ(TurnDirectionFromHeadingDelta(-0.20), -1);
    EXPECT_EQ(TurnDirectionFromHeadingDelta(0.02), 0);
}

}  // namespace gt_esmini
