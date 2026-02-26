#pragma once

namespace gt_esmini
{
class ControllerRealDriver;

class VehicleStateUpdater
{
public:
    void UpdatePhysics(ControllerRealDriver& controller, double time_step) const;
};
} // namespace gt_esmini
