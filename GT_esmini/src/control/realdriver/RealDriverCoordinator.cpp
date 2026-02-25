#include "gt_esmini/control/realdriver/RealDriverCoordinator.hpp"

#include "gt_esmini/control/ControllerRealDriver.hpp"
#include "gt_esmini/control/ControlDecisionEngine.hpp"
#include "gt_esmini/control/DriverInputReceiver.hpp"
#include "gt_esmini/control/EsminiStateApplier.hpp"
#include "gt_esmini/control/VehicleStateUpdater.hpp"
#include "gt_esmini/control/realdriver/DriverOutputPort.hpp"
#include "gt_esmini/control/realdriver/LatPathPlanner.hpp"

namespace gt_esmini
{
void RealDriverCoordinator::RunFrame(ControllerRealDriver& controller, double time_step) const
{
    controller.control_decision_engine_->UpdateSetSpeed(controller);
    controller.driver_input_receiver_->Receive(controller);
    controller.vehicle_state_updater_->UpdatePhysics(controller, time_step);

    controller.lon_profile_planner_->Advance(time_step, controller.setSpeed_);
    const auto lon_profile = controller.lon_profile_planner_->BuildProfile(controller.currentSpeed_);
    controller.driver_output_port_->SendLonProfile(controller, lon_profile);

    controller.MaybeSendWaypoints();
    controller.UpdateCachedPowertrain();
    controller.UpdateHostVehicleReporter();

    double combined_pitch = 0.0;
    double combined_roll = 0.0;
    controller.real_vehicle_.GetCombinedAttitude(combined_pitch, combined_roll);

    if (controller.object_ && controller.gateway_)
    {
        controller.lat_path_planner_->HandleActions(controller, "");

        auto state_for_long = controller.GetRunningActionState();
        controller.esmini_state_applier_->Apply(controller, combined_pitch, combined_roll, state_for_long.hasRunningScenarioLongAction);
        controller.UpdateVehicleLights();
    }

    controller.scenarioengine::Controller::Step(time_step);
    controller.RefreshWaypointsOnRoutePointerChange();
}
} // namespace gt_esmini
