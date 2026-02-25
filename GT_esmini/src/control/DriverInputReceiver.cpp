#include "gt_esmini/control/DriverInputReceiver.hpp"
#include "gt_esmini/control/ControllerRealDriver.hpp"

namespace gt_esmini
{
void DriverInputReceiver::Receive(ControllerRealDriver& controller) const
{
    controller.ReceiveLatestUdpInput();
}
} // namespace gt_esmini
