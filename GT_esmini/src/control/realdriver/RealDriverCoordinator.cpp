#include "gt_esmini/control/realdriver/RealDriverCoordinator.hpp"

#include "gt_esmini/control/ControllerRealDriver.hpp"
#include "gt_esmini/control/realdriver/DriverOutputPort.hpp"
#include "gt_esmini/control/realdriver/LatPathPlanner.hpp"

namespace gt_esmini
{
void RealDriverCoordinator::RunFrame(ControllerRealDriver& controller, double time_step) const
{
    controller.UpdateSetSpeedFromScenarioObject();
    controller.ReceiveLatestUdpInput();
    controller.UpdateVehiclePhysics(time_step);

    controller.lon_profile_planner_->Advance(time_step, controller.setSpeed_);
    const auto lon_profile = controller.lon_profile_planner_->BuildProfile(controller.currentSpeed_);
    controller.driver_output_port_->SendLonProfile(controller, lon_profile);

    controller.MaybeSendWaypoints();
    controller.UpdateCachedPowertrain();
    controller.UpdateHostVehicleReporter();

    double combined_pitch = 0.0;
    double combined_roll = 0.0;
    controller.real_vehicle_.GetCombinedAttitude(combined_pitch, combined_roll);

    if (controller.object_)
    {
        controller.lat_path_planner_->HandleActions(controller, "");

        auto state_for_long = controller.GetRunningActionState();
        controller.SyncGatewayObjectState(combined_pitch, combined_roll, state_for_long.hasRunningScenarioLongAction);
        controller.UpdateVehicleLights();
    }

    controller.scenarioengine::Controller::Step(time_step);
    controller.RefreshWaypointsOnRoutePointerChange();
}
} // namespace gt_esmini
