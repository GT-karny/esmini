#pragma once

#include "CommonMini.hpp"
#include "RoadManager.hpp"

#include <cmath>

namespace gt_esmini
{

constexpr double kJunctionSharpTurnRateRadPerMeter = 0.04;
constexpr double kJunctionTurnHeadingThresholdRad  = 0.10;

inline bool IsSharpJunctionConnector(const roadmanager::Road* road,
                                     double turn_rate_threshold = kJunctionSharpTurnRateRadPerMeter)
{
    if (!road || road->GetJunction() == ID_UNDEFINED || road->GetLength() <= 0.1)
    {
        return false;
    }

    roadmanager::Position start;
    roadmanager::Position end;
    start.SetTrackPos(road->GetId(), 0.0, 0.0);
    end.SetTrackPos(road->GetId(), road->GetLength(), 0.0);

    const double heading_delta = std::fabs(GetAngleInIntervalMinusPIPlusPI(end.GetH() - start.GetH()));
    return heading_delta / road->GetLength() >= turn_rate_threshold;
}

inline int TurnDirectionFromHeadingDelta(double heading_delta,
                                         double threshold = kJunctionTurnHeadingThresholdRad)
{
    heading_delta = GetAngleInIntervalMinusPIPlusPI(heading_delta);
    if (heading_delta > threshold) return 1;
    if (heading_delta < -threshold) return -1;
    return 0;
}

// Driver-frame direction of a lane change: +1 = left, -1 = right, 0 = none.
//
// OpenDRIVE lane ids are signed relative to the road's s-direction: a HIGHER id is always further
// to the LEFT when looking along +s. That convention is purely geometric, so it is independent of
// the road's LHT/RHT rule -- only the direction of travel matters. A vehicle running against s sees
// road-left as its own right, hence the flip.
//
// This must NOT be derived from the lateral offset of a preview point in the vehicle body frame:
// during a lane change the ego's own yaw contributes more apparent lateral offset at a multi-second
// preview distance than the lane displacement itself (60 m x 0.07 rad ~ 4 m vs. a 3.5 m lane), so
// the sign inverts mid-manoeuvre and the wrong indicator lights up.
inline int LaneChangeIndicatorDir(int current_lane_id, int target_lane_id, bool travelling_along_s)
{
    if (target_lane_id == current_lane_id)
    {
        return 0;
    }
    const int road_frame_dir = (target_lane_id > current_lane_id) ? 1 : -1;
    return travelling_along_s ? road_frame_dir : -road_frame_dir;
}

inline int RouteLookaheadJunctionTurnDirection(const roadmanager::Position& start,
                                               roadmanager::OpenDrive* odr,
                                               double lookahead,
                                               double step = 2.0)
{
    if (!odr) return 0;

    roadmanager::Position pos;
    pos.Duplicate(start);
    pos.CopyRoute(start);

    const double current_heading = start.GetDrivingDirection();
    const id_t current_track = start.GetTrackId();
    double traveled = 0.0;
    bool passed_junction = false;

    while (traveled < lookahead)
    {
        const int ret = static_cast<int>(pos.MoveAlongS(step));
        if (ret == static_cast<int>(roadmanager::Position::ReturnCode::ERROR_GENERIC)) break;
        traveled += step;

        const id_t track = pos.GetTrackId();
        if (track == current_track) continue;

        roadmanager::Road* road = odr->GetRoadById(track);
        if (!road) continue;

        if (road->GetJunction() != ID_UNDEFINED)
        {
            passed_junction = true;
            continue;
        }
        if (!passed_junction) return 0;

        return TurnDirectionFromHeadingDelta(pos.GetDrivingDirection() - current_heading);
    }
    return 0;
}

}  // namespace gt_esmini
