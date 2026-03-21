#pragma once

#include "osi_hostvehicledata.pb.h"

namespace scenarioengine
{
class ScenarioGateway;
class Object;
} // namespace scenarioengine

namespace gt_esmini
{

class HVDStateApplier
{
public:
    void Apply(scenarioengine::ScenarioGateway* gateway,
               scenarioengine::Object* object,
               double pos_x, double pos_y, double pos_z,
               double heading, double speed, double wheel_angle,
               double body_offset_x, double body_offset_y, double body_offset_z,
               double combined_pitch, double combined_roll,
               bool block_speed_update) const;
};

} // namespace gt_esmini
