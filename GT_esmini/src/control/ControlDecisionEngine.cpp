#include "gt_esmini/control/ControlDecisionEngine.hpp"
#include "gt_esmini/control/ControllerRealDriver.hpp"

namespace gt_esmini
{
void ControlDecisionEngine::UpdateSetSpeed(ControllerRealDriver& controller) const
{
    controller.UpdateSetSpeedFromScenarioObject();
}
} // namespace gt_esmini
