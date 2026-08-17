/*
 * GT_esmini - Extended esmini
 * https://github.com/esmini/esmini
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2026 GT_esmini contributors
 */

#pragma once

// feature:F9 -- the esmini <-> SUMO pose conversion, isolated from libsumo so it
// can be unit tested without loading a simulation.
//
// The three conversions here are exactly the three defects measured on upstream
// ControllerSumo in this build (design doc GT_esmini/docs/features/
// sumo_background_traffic.md, section 2), where all three are missing:
//
//   heading  : SUMO wants navigational degrees (0 = north, clockwise); esmini
//              carries a mathematical heading in radians (0 = +X, counter
//              clockwise). Upstream hands the radian value to the degree
//              argument. On an east-bound road that looks almost right
//              (1.57 vs 0.19) and on a +X-bound road it is 90 degrees off.
//   position : SUMO's getPosition3D()/moveToXY() reference the front bumper
//              centre, esmini's pos_ is the OpenSCENARIO reference point.
//   pitch    : SUMO's getSlope() is positive uphill, OpenDRIVE/esmini pitch is
//              negative uphill.
//
// Net offset is folded in here too so that one composite call carries the whole
// frame change -- a caller that reaches for the primitives one at a time is how
// upstream ended up applying two of the four terms and none of the rest.

namespace gt_esmini
{
namespace sumotraffic
{

struct Vec2
{
    double x = 0.0;
    double y = 0.0;
};

// Pose in esmini/OpenDRIVE terms: OpenSCENARIO reference point + mathematical
// heading [rad], both in the esmini inertial frame.
struct EsminiPose
{
    double x           = 0.0;
    double y           = 0.0;
    double heading_rad = 0.0;
};

// Pose in SUMO terms: front-bumper centre + navigational angle [deg], both in
// the SUMO network frame (esmini frame + netOffset).
struct SumoPose
{
    double x         = 0.0;
    double y         = 0.0;
    double angle_deg = 0.0;
};

// Longitudinal distance from the OpenSCENARIO reference point to the front
// bumper centre. Same expression Object::OverlappingFront() uses to build the
// front line of the bounding box.
double FrontOffset(double bb_center_x, double bb_length);

// Mathematical heading [rad] -> navigational angle [deg], wrapped to [0, 360).
double HeadingToSumoAngle(double heading_rad);

// Navigational angle [deg] -> mathematical heading [rad], wrapped to [0, 2*pi).
double SumoAngleToHeading(double angle_deg);

// SUMO slope [deg, positive uphill] -> esmini pitch [rad, negative uphill].
double SumoSlopeToPitch(double slope_deg);

// esmini pitch [rad, negative uphill] -> SUMO slope [deg, positive uphill].
double PitchToSumoSlope(double pitch_rad);

// Reference point -> front bumper centre (projection along the heading).
Vec2 RefPointToFront(const Vec2& ref, double heading_rad, double bb_center_x, double bb_length);

// Front bumper centre -> reference point.
Vec2 FrontToRefPoint(const Vec2& front, double heading_rad, double bb_center_x, double bb_length);

// Composite conversions. net_offset is the SUMO net's <location netOffset>:
// sumo_xy = esmini_xy + net_offset.
SumoPose   ToSumoPose(const EsminiPose& pose, const Vec2& net_offset, double bb_center_x, double bb_length);
EsminiPose ToEsminiPose(const SumoPose& pose, const Vec2& net_offset, double bb_center_x, double bb_length);

// Heading derived from the displacement between two consecutive positions.
// Returns false (and leaves heading_rad_out untouched) when the displacement is
// shorter than min_dist -- below that the direction is dominated by noise and
// the caller must keep its previous heading.
//
// This is the yaw source for SUMO-driven vehicles: libsumo 1.6.0's own
// computeAngle() only reports a lane-change yaw offset while the
// --lanechange.duration animation is running, and reports none at all under the
// sublane model (design doc section 3-6).
bool HeadingFromDisplacement(const Vec2& prev, const Vec2& cur, double min_dist, double& heading_rad_out);

}  // namespace sumotraffic
}  // namespace gt_esmini
