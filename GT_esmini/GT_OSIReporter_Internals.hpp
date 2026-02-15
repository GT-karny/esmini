#pragma once

#include "OSIReporter.hpp"

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
