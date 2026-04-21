#include "gt_esmini/control/manualdrive/HVDStateApplier.hpp"

#include "Entities.hpp"

#include <cmath>

namespace gt_esmini
{

void HVDStateApplier::Apply(scenarioengine::Object* object,
                            double pos_x, double pos_y, double pos_z,
                            double heading, double speed, double wheel_angle,
                            double body_offset_x, double body_offset_y, double body_offset_z,
                            double combined_pitch, double combined_roll,
                            bool block_speed_update) const
{
    (void)pos_z;
    if (!object)
    {
        return;
    }

    // Transform body offset from vehicle-local to world frame
    const double h = heading;
    const double w_dx = body_offset_x * std::cos(h) - body_offset_y * std::sin(h);
    const double w_dy = body_offset_x * std::sin(h) + body_offset_y * std::cos(h);

    // v3.0.0: Gateway removed — write directly to Object
    object->pos_.SetInertiaPos(
        pos_x + w_dx, pos_y + w_dy, body_offset_z,
        heading, combined_pitch, combined_roll);
    object->dirty_.SetBits(scenarioengine::Object::DirtyBit::LATERAL | scenarioengine::Object::DirtyBit::LONGITUDINAL);

    if (!block_speed_update)
    {
        object->SetSpeed(speed);
    }

    object->wheel_angle_ = wheel_angle;
    object->dirty_.SetBits(scenarioengine::Object::DirtyBit::WHEEL_ANGLE);
}

} // namespace gt_esmini
