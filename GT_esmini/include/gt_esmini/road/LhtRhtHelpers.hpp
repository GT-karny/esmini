#pragma once

#include "RoadManager.hpp"

namespace gt_esmini::road
{

inline int DrivingLaneSign(roadmanager::Road::RoadRule rule)
{
    return (rule == roadmanager::Road::RoadRule::LEFT_HAND_TRAFFIC) ? +1 : -1;
}

inline bool IsForwardSLane(int laneId, roadmanager::Road::RoadRule rule)
{
    if (laneId == 0)
    {
        return false;
    }
    return (laneId * DrivingLaneSign(rule)) > 0;
}

inline bool SameDrivingSide(int laneIdA, int laneIdB, roadmanager::Road::RoadRule /*rule*/)
{
    if (laneIdA == 0 || laneIdB == 0)
    {
        return false;
    }
    return (laneIdA > 0) == (laneIdB > 0);
}

}
