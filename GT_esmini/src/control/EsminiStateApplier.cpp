#include "gt_esmini/control/EsminiStateApplier.hpp"
#include "gt_esmini/control/ControllerRealDriver.hpp"

namespace gt_esmini
{
void EsminiStateApplier::Apply(ControllerRealDriver& controller,
                               double combined_pitch,
                               double combined_roll,
                               bool block_speed_update) const
{
    controller.SyncGatewayObjectState(combined_pitch, combined_roll, block_speed_update);
}
} // namespace gt_esmini
