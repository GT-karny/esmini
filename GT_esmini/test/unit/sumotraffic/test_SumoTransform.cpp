/*
 * feature:F9 -- esmini <-> SUMO pose conversion.
 *
 * The anchors are the values measured on upstream ControllerSumo in this build
 * and written down in GT_esmini/docs/features/sumo_background_traffic.md
 * section 2. Each conversion is fixed from both sides: the value the correct
 * transform must produce, AND the value the missing transform (identity)
 * produces -- because on the road the measurement was taken from, an east-bound
 * highway, the two are only 1.4 degrees apart and "close enough" is exactly how
 * the defect survived.
 */

#include <cmath>

#include "gtest/gtest.h"

#include "CommonMini.hpp"  // M_PI, GetAngleInInterval2PI (the inverse upstream uses)
#include "gt_esmini/control/sumotraffic/SumoTransform.hpp"

using namespace gt_esmini::sumotraffic;

namespace
{
// The doc's heading column is logged to 4 decimals, so the derived navigational
// angle carries up to 5e-5 rad = 0.0029 deg of rounding. The tolerance has to
// clear that and nothing more.
constexpr double kNavAngleTol = 3e-3;

// cut-in_sumo.xosc Ego bounding box: reference point 2.0 m behind the box
// centre, box 5.0 m long -> the front bumper centre sits 4.5 m ahead.
constexpr double kEgoBbCenterX = 2.0;
constexpr double kEgoBbLength  = 5.0;
constexpr double kEgoFrontOffset = 4.5;
}  // namespace

// ---------------------------------------------------------------- heading ---

TEST(SumoTransform, HeadingConvertsToMeasuredNavigationalDegrees)
{
    // design doc 2-2, the three sampled instants of cut-in_sumo.xosc
    EXPECT_NEAR(HeadingToSumoAngle(1.5674), 0.194, kNavAngleTol);
    EXPECT_NEAR(HeadingToSumoAngle(1.5660), 0.273, kNavAngleTol);
    EXPECT_NEAR(HeadingToSumoAngle(1.5245), 2.653, kNavAngleTol);
}

TEST(SumoTransform, HeadingIdentityIsWhatUpstreamSendsAndItIsWrong)
{
    // Upstream hands obj->pos_.GetH() (radians) straight to the degrees
    // argument. On this east-bound road that lands at 1.570 where 0.194 is
    // correct -- small enough to look like noise.
    const double h = 1.5674;
    EXPECT_NEAR(h, 1.570, 3e-3) << "the identity 'conversion' upstream performs";
    EXPECT_GT(std::fabs(HeadingToSumoAngle(h) - h), 1.3) << "correct and identity must not be confusable";

    // The same identity on a +X-bound road is 90 degrees out. This is the case
    // that makes the defect visible, and the reason the east-bound measurement
    // must never be used as evidence that the conversion is fine.
    EXPECT_NEAR(HeadingToSumoAngle(0.0), 90.0, 1e-9);
    EXPECT_NEAR(HeadingToSumoAngle(M_PI), 270.0, 1e-9);
    EXPECT_NEAR(HeadingToSumoAngle(M_PI_2), 0.0, 1e-9);
    EXPECT_NEAR(HeadingToSumoAngle(-M_PI_2), 180.0, 1e-9);
}

TEST(SumoTransform, HeadingRoundTripsThroughSumoAngle)
{
    for (double h = 0.0; h < 2.0 * M_PI; h += 0.37)
    {
        EXPECT_NEAR(SumoAngleToHeading(HeadingToSumoAngle(h)), h, 1e-12) << "h = " << h;
    }
}

TEST(SumoTransform, SumoAngleMatchesTheInverseUpstreamAlreadyUses)
{
    // Reading SUMO back is the one direction upstream gets right; keeping the
    // same expression here is deliberate.
    for (double angle = 0.0; angle < 360.0; angle += 17.0)
    {
        const double expected = GetAngleInInterval2PI(-angle * M_PI / 180.0 + M_PI / 2.0);
        EXPECT_NEAR(SumoAngleToHeading(angle), expected, 1e-12) << "angle = " << angle;
    }
}

// -------------------------------------------------------- reference point ---

TEST(SumoTransform, FrontOffsetIsBoxCentrePlusHalfLength)
{
    EXPECT_NEAR(FrontOffset(kEgoBbCenterX, kEgoBbLength), kEgoFrontOffset, 1e-12);
    // A box whose centre is already the reference point still has a front face.
    EXPECT_NEAR(FrontOffset(0.0, 4.0), 2.0, 1e-12);
}

TEST(SumoTransform, ReferencePointShiftsToTheFrontBumperAlongHeading)
{
    const Vec2 ref{100.0, 50.0};

    // +X bound
    Vec2 front = RefPointToFront(ref, 0.0, kEgoBbCenterX, kEgoBbLength);
    EXPECT_NEAR(front.x, 104.5, 1e-9);
    EXPECT_NEAR(front.y, 50.0, 1e-9);

    // north bound
    front = RefPointToFront(ref, M_PI_2, kEgoBbCenterX, kEgoBbLength);
    EXPECT_NEAR(front.x, 100.0, 1e-9);
    EXPECT_NEAR(front.y, 54.5, 1e-9);
}

TEST(SumoTransform, ReferencePointIdentityIsWhatUpstreamSendsAndItIsWrong)
{
    // design doc 2-3: the forward component between esmini's reference point and
    // SUMO's position measured -0.003 .. -0.005 m, i.e. no shift at all, where
    // +4.5 m was expected.
    const Vec2   ref{100.0, 50.0};
    const double heading = 1.5674;  // same instant as the heading anchors

    const Vec2   front   = RefPointToFront(ref, heading, kEgoBbCenterX, kEgoBbLength);
    const double forward = (front.x - ref.x) * cos(heading) + (front.y - ref.y) * sin(heading);
    const double lateral = -(front.x - ref.x) * sin(heading) + (front.y - ref.y) * cos(heading);

    EXPECT_NEAR(forward, kEgoFrontOffset, 1e-9);
    EXPECT_NEAR(lateral, 0.0, 1e-9);
    EXPECT_GT(std::fabs(forward - 0.0), 4.0) << "must be distinguishable from the measured no-op";
}

TEST(SumoTransform, FrontToReferencePointIsTheExactInverse)
{
    const Vec2 ref{-31.25, 812.5};
    for (double h = 0.0; h < 2.0 * M_PI; h += 0.41)
    {
        const Vec2 back = FrontToRefPoint(RefPointToFront(ref, h, kEgoBbCenterX, kEgoBbLength), h, kEgoBbCenterX, kEgoBbLength);
        EXPECT_NEAR(back.x, ref.x, 1e-9) << "h = " << h;
        EXPECT_NEAR(back.y, ref.y, 1e-9) << "h = " << h;
    }
}

// -------------------------------------------------------------- composite ---

TEST(SumoTransform, CompositePoseCarriesOffsetFrontAndAngleTogether)
{
    const Vec2       net_offset{1000.0, -250.0};
    const EsminiPose pose{100.0, 50.0, M_PI_2};

    const SumoPose sumo = ToSumoPose(pose, net_offset, kEgoBbCenterX, kEgoBbLength);
    EXPECT_NEAR(sumo.x, 100.0 + 1000.0, 1e-9);
    EXPECT_NEAR(sumo.y, 50.0 + 4.5 - 250.0, 1e-9);
    EXPECT_NEAR(sumo.angle_deg, 0.0, 1e-9);
}

TEST(SumoTransform, CompositePoseRoundTrips)
{
    const Vec2 net_offset{1000.0, -250.0};
    for (double h = 0.0; h < 2.0 * M_PI; h += 0.29)
    {
        const EsminiPose pose{123.75, -44.5, h};
        const EsminiPose back = ToEsminiPose(ToSumoPose(pose, net_offset, kEgoBbCenterX, kEgoBbLength), net_offset, kEgoBbCenterX, kEgoBbLength);
        EXPECT_NEAR(back.x, pose.x, 1e-9) << "h = " << h;
        EXPECT_NEAR(back.y, pose.y, 1e-9) << "h = " << h;
        EXPECT_NEAR(back.heading_rad, h, 1e-12) << "h = " << h;
    }
}

TEST(SumoTransform, CompositePoseIsNotTheIdentity)
{
    // The negative control for the whole conversion: with a non-zero netOffset,
    // a non-zero front offset and a heading that is not already navigational,
    // every one of the three terms has to move the result.
    const Vec2       net_offset{1000.0, -250.0};
    const EsminiPose pose{100.0, 50.0, 0.0};

    const SumoPose sumo = ToSumoPose(pose, net_offset, kEgoBbCenterX, kEgoBbLength);
    EXPECT_GT(std::fabs(sumo.x - pose.x), 1.0);
    EXPECT_GT(std::fabs(sumo.y - pose.y), 1.0);
    EXPECT_GT(std::fabs(sumo.angle_deg - pose.heading_rad), 1.0);
}

// ------------------------------------------------------------------ pitch ---

TEST(SumoTransform, SlopeSignIsInvertedIntoPitch)
{
    // SUMO: positive uphill. OpenDRIVE/esmini pitch: negative uphill.
    EXPECT_NEAR(SumoSlopeToPitch(5.0), -5.0 * M_PI / 180.0, 1e-12);
    EXPECT_NEAR(SumoSlopeToPitch(-5.0), 5.0 * M_PI / 180.0, 1e-12);
    EXPECT_NEAR(SumoSlopeToPitch(0.0), 0.0, 1e-12);

    // and the sign flip is the whole point: passing it through unchanged (what
    // upstream does) would put the nose the wrong way on any graded road.
    EXPECT_LT(SumoSlopeToPitch(5.0) * (5.0 * M_PI / 180.0), 0.0);

    EXPECT_NEAR(PitchToSumoSlope(SumoSlopeToPitch(7.5)), 7.5, 1e-12);
}

// ------------------------------------------------- heading from history ----

TEST(SumoTransform, HeadingFromDisplacementFollowsTheDirectionOfTravel)
{
    double heading = -1.0;

    EXPECT_TRUE(HeadingFromDisplacement(Vec2{0.0, 0.0}, Vec2{1.0, 0.0}, 0.05, heading));
    EXPECT_NEAR(heading, 0.0, 1e-12);

    EXPECT_TRUE(HeadingFromDisplacement(Vec2{0.0, 0.0}, Vec2{0.0, 1.0}, 0.05, heading));
    EXPECT_NEAR(heading, M_PI_2, 1e-12);

    // A lane change to the left while heading +X: a small positive yaw.
    EXPECT_TRUE(HeadingFromDisplacement(Vec2{0.0, 0.0}, Vec2{0.70, 0.05}, 0.05, heading));
    EXPECT_GT(heading, 0.0);
    EXPECT_LT(heading, 0.2);
}

TEST(SumoTransform, HeadingFromDisplacementRejectsNoiseAndKeepsTheOldValue)
{
    double heading = 1.25;
    EXPECT_FALSE(HeadingFromDisplacement(Vec2{0.0, 0.0}, Vec2{0.01, 0.01}, 0.05, heading));
    EXPECT_NEAR(heading, 1.25, 1e-12) << "the out-parameter must be left alone on rejection";

    // Standing still is the degenerate case that would otherwise produce
    // atan2(0,0) and spin the vehicle to 0 rad.
    EXPECT_FALSE(HeadingFromDisplacement(Vec2{5.0, 5.0}, Vec2{5.0, 5.0}, 0.05, heading));
    EXPECT_NEAR(heading, 1.25, 1e-12);
}
