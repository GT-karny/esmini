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
