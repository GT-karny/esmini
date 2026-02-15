#include "gt_esmini/control/VehicleStateUpdater.hpp"
#include "gt_esmini/control/ControllerRealDriver.hpp"

namespace gt_esmini
{
void VehicleStateUpdater::UpdatePhysics(ControllerRealDriver& controller, double time_step) const
{
    controller.UpdateVehiclePhysics(time_step);
}
} // namespace gt_esmini
