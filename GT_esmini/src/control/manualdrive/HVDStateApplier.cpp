#include "gt_esmini/control/manualdrive/HVDStateApplier.hpp"

#include "ScenarioGateway.hpp"
#include "Entities.hpp"

#include <cmath>

namespace gt_esmini
{

void HVDStateApplier::Apply(scenarioengine::ScenarioGateway* gateway,
                            scenarioengine::Object* object,
                            double pos_x, double pos_y, double pos_z,
                            double heading, double speed, double wheel_angle,
                            double body_offset_x, double body_offset_y, double body_offset_z,
                            double combined_pitch, double combined_roll,
                            bool block_speed_update) const
{
    if (!gateway || !object)
    {
        return;
    }

    // Transform body offset from vehicle-local to world frame
    const double h = heading;
    const double w_dx = body_offset_x * std::cos(h) - body_offset_y * std::sin(h);
    const double w_dy = body_offset_x * std::sin(h) + body_offset_y * std::cos(h);

    // Sync to gateway (same sequence as ControllerRealDriver::SyncGatewayObjectState)
    gateway->updateObjectWorldPosXYH(object->id_, 0.0, pos_x + w_dx, pos_y + w_dy, heading);

    if (!block_speed_update)
    {
        gateway->updateObjectSpeed(object->id_, 0.0, speed);
    }

    gateway->updateObjectWheelAngle(object->id_, 0.0, wheel_angle);

    gateway->updateObjectWorldPos(
        object->id_, 0.0,
        pos_x + w_dx, pos_y + w_dy, pos_z + body_offset_z,
        heading, combined_pitch, combined_roll);

    // Sync object pose for internal consistency
    object->pos_.SetInertiaPos(pos_x + w_dx, pos_y + w_dy, heading);
    object->SetDirtyBits(scenarioengine::Object::DirtyBit::LATERAL | scenarioengine::Object::DirtyBit::LONGITUDINAL);
}

} // namespace gt_esmini
