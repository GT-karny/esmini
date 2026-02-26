#include "gt_esmini/control/realdriver/LatPathPlanner.hpp"
#include "gt_esmini/control/ControllerRealDriver.hpp"
#include "OSCPrivateAction.hpp"

#include <array>

namespace gt_esmini
{
bool LatPathPlanner::HandleActions(ControllerRealDriver& controller, const char* phase_label) const
{
    // Domain-first selection: choose exactly one active LAT action, preferring newest action id.
    auto state = controller.GetRunningActionState();

    std::array<scenarioengine::OSCPrivateAction*, 4> actions = {
        static_cast<scenarioengine::OSCPrivateAction*>(state.followTrajectory),
        static_cast<scenarioengine::OSCPrivateAction*>(state.laneChange),
        static_cast<scenarioengine::OSCPrivateAction*>(state.laneOffset),
        static_cast<scenarioengine::OSCPrivateAction*>(state.assignRoute),
    };

    scenarioengine::OSCPrivateAction* selected = nullptr;
    for (auto* action : actions)
    {
        if (!action)
        {
            continue;
        }
        if (!selected || action->GetId() > selected->GetId())
        {
            selected = action;
        }
    }

    if (!selected)
    {
        return false;
    }

    ControllerRealDriver::RunningActionState selected_state{};
    selected_state.hasRunningScenarioLongAction = state.hasRunningScenarioLongAction;

    if (selected == state.followTrajectory)
    {
        selected_state.followTrajectory = state.followTrajectory;
    }
    else if (selected == state.laneChange)
    {
        selected_state.laneChange = state.laneChange;
    }
    else if (selected == state.laneOffset)
    {
        selected_state.laneOffset = state.laneOffset;
    }
    else if (selected == state.assignRoute)
    {
        selected_state.assignRoute = state.assignRoute;
    }

    const ControllerRealDriver::ActionFlags previous_flags{};
    return controller.HandlePathActions(selected_state, previous_flags, phase_label);
}
} // namespace gt_esmini
