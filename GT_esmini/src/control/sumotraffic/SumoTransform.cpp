/*
 * GT_esmini - Extended esmini
 *
 * feature:F9 -- esmini <-> SUMO pose conversion. See the header for why each of
 * these exists; every one of them is a term upstream ControllerSumo omits.
 */

#include "gt_esmini/control/sumotraffic/SumoTransform.hpp"

#include <cmath>

#include "CommonMini.hpp"

namespace gt_esmini
{
namespace sumotraffic
{

double FrontOffset(double bb_center_x, double bb_length)
{
    return bb_center_x + bb_length / 2.0;
}

double HeadingToSumoAngle(double heading_rad)
{
    // navigational: 0 = north (+Y), growing clockwise. mathematical: 0 = +X,
    // growing counter clockwise. The two are related by a mirror about the
    // 45 degree line: nav = 90 - math.
    return GetAngleInInterval2PI(M_PI / 2.0 - heading_rad) * 180.0 / M_PI;
}

double SumoAngleToHeading(double angle_deg)
{
    // Same mirror, applied the other way. Identical in form to the expression
    // upstream ControllerSumo already uses when reading SUMO back
    // (-getAngle()*M_PI/180 + M_PI/2); only the outbound direction was missing.
    return GetAngleInInterval2PI(-angle_deg * M_PI / 180.0 + M_PI / 2.0);
}

double SumoSlopeToPitch(double slope_deg)
{
    return -slope_deg * M_PI / 180.0;
}

double PitchToSumoSlope(double pitch_rad)
{
    return -pitch_rad * 180.0 / M_PI;
}

Vec2 RefPointToFront(const Vec2& ref, double heading_rad, double bb_center_x, double bb_length)
{
    const double offset = FrontOffset(bb_center_x, bb_length);
    return Vec2{ref.x + offset * cos(heading_rad), ref.y + offset * sin(heading_rad)};
}

Vec2 FrontToRefPoint(const Vec2& front, double heading_rad, double bb_center_x, double bb_length)
{
    const double offset = FrontOffset(bb_center_x, bb_length);
    return Vec2{front.x - offset * cos(heading_rad), front.y - offset * sin(heading_rad)};
}

SumoPose ToSumoPose(const EsminiPose& pose, const Vec2& net_offset, double bb_center_x, double bb_length)
{
    const Vec2 front = RefPointToFront(Vec2{pose.x, pose.y}, pose.heading_rad, bb_center_x, bb_length);
    return SumoPose{front.x + net_offset.x, front.y + net_offset.y, HeadingToSumoAngle(pose.heading_rad)};
}

EsminiPose ToEsminiPose(const SumoPose& pose, const Vec2& net_offset, double bb_center_x, double bb_length)
{
    const double heading = SumoAngleToHeading(pose.angle_deg);
    const Vec2   ref     = FrontToRefPoint(Vec2{pose.x - net_offset.x, pose.y - net_offset.y}, heading, bb_center_x, bb_length);
    return EsminiPose{ref.x, ref.y, heading};
}

bool HeadingFromDisplacement(const Vec2& prev, const Vec2& cur, double min_dist, double& heading_rad_out)
{
    const double dx = cur.x - prev.x;
    const double dy = cur.y - prev.y;
    if (sqrt(dx * dx + dy * dy) < min_dist)
    {
        return false;
    }
    heading_rad_out = GetAngleInInterval2PI(atan2(dy, dx));
    return true;
}

}  // namespace sumotraffic
}  // namespace gt_esmini
