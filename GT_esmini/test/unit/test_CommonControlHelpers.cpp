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

// docs/virtualdriver/design/junction_turn_signal.md section 2-1: sign convention
// for a connector's turn direction. ConnectorTurnDirectionFromHeadingDelta is the
// pure core of ConnectorTurnDirection (same file), decoupled from Position/Road so
// this fixes the sign convention without constructing OpenDRIVE geometry.
TEST(JunctionTurnHelperTest, ConnectorTurnDirectionFlipsSignWithTravelDirection)
{
    // s-increasing travel (trav_dir = +1): raw heading delta sign passes through.
    EXPECT_EQ(ConnectorTurnDirectionFromHeadingDelta(0.5, 1), 1);
    EXPECT_EQ(ConnectorTurnDirectionFromHeadingDelta(-0.5, 1), -1);
    // s-decreasing travel (trav_dir = -1): the road-frame heading delta was taken
    // in the OPPOSITE order the route actually traverses it, so the sign flips.
    EXPECT_EQ(ConnectorTurnDirectionFromHeadingDelta(0.5, -1), -1);
    EXPECT_EQ(ConnectorTurnDirectionFromHeadingDelta(-0.5, -1), 1);
}

TEST(JunctionTurnHelperTest, ConnectorTurnDirectionZeroOnStraightConnector)
{
    // Below kJunctionTurnHeadingThresholdRad (0.10 rad); e.g. cross_straight_junction's
    // ~0.046 rad measured delta (design doc section 3-1) must fall inside this margin.
    EXPECT_EQ(ConnectorTurnDirectionFromHeadingDelta(0.046, 1), 0);
    EXPECT_EQ(ConnectorTurnDirectionFromHeadingDelta(0.0, -1), 0);
}

TEST(JunctionTurnHelperTest, ConnectorTurnDirectionZeroForNullOrNonJunctionRoad)
{
    EXPECT_EQ(ConnectorTurnDirection(nullptr, 1), 0);

    roadmanager::Road not_junction(1, "1", "not_junction");
    not_junction.SetLength(20.0);  // GetJunction() defaults to ID_UNDEFINED
    EXPECT_EQ(ConnectorTurnDirection(&not_junction, 1), 0);

    roadmanager::Road too_short(2, "2", "junction_but_too_short");
    too_short.SetJunction(5);
    too_short.SetLength(0.05);  // <= the 0.1 m floor IsSharpJunctionConnector also uses
    EXPECT_EQ(ConnectorTurnDirection(&too_short, 1), 0);
}

}  // namespace gt_esmini
