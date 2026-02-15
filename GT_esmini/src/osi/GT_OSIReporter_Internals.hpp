#pragma once

#include "OSIReporter.hpp"
#include "gt_esmini/scenario/ExtraEntities.hpp"
#include <functional>
#include <string>

struct OsiInternalObjects
{
    osi3::SensorData                 *sd;
    osi3::GroundTruth                *static_gt;
    osi3::GroundTruth                *static_updated_gt;
    osi3::GroundTruth                *dynamic_gt;
    osi3::StationaryObject           *sobj;
    osi3::TrafficSign                *ts;
    osi3::MovingObject               *mobj;
    std::vector<osi3::Lane *>         ln;
    std::vector<osi3::LaneBoundary *> lnb;
};

struct OsiExternalObjects
{
    osi3::GroundTruth    *gt;
    osi3::SensorView     *sv;
    osi3::TrafficCommand *tc;
};

extern OsiInternalObjects obj_osi_internal;
extern OsiExternalObjects obj_osi_external;
extern std::function<::gt_esmini::LightState(void*, int)> g_LightStateProvider;

struct OsiTrafficCommandBuffer
{
    std::string  traffic_command;
    unsigned int size;
};

struct OsiGroundTruthBuffer
{
    std::string  ground_truth;
    unsigned int size;
};

struct OsiRoadLaneBuffer
{
    std::string  osi_lane_info;
    unsigned int size;
};

struct OsiRoadLaneBoundaryBuffer
{
    std::string  osi_lane_boundary_info;
    unsigned int size;
};

extern OsiTrafficCommandBuffer osiTrafficCommand;
extern OsiGroundTruthBuffer osiGroundTruth;
extern OsiRoadLaneBuffer osiRoadLane;
extern OsiRoadLaneBoundaryBuffer osiRoadLaneBoundary;
