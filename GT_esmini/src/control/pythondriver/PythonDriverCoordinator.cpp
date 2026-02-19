#ifdef GT_ENABLE_EMBEDDED_PYTHON

#include "gt_esmini/control/pythondriver/PythonDriverCoordinator.hpp"

#include "gt_esmini/control/ControllerPythonDriver.hpp"
#include "gt_esmini/control/pythondriver/PythonDriverBridge.hpp"

#include "esminiLib.hpp" // For SE_GetOSIGroundTruth

namespace gt_esmini
{
void PythonDriverCoordinator::RunFrame(ControllerPythonDriver& controller, double time_step) const
{
    if (controller.HasFatalError())
    {
        return;
    }

    // 1. Update target speed from scenario actions
    controller.UpdateSetSpeedFromScenarioObject();

    // 2. Ensure waypoints are extracted from route
    controller.EnsureWaypointsExtracted();

    // 3. Build longitudinal speed profile
    const auto lon_profile = controller.lon_profile_planner_->BuildProfile(controller.currentSpeed_, controller.setSpeed_);

    // 4. Get OSI GroundTruth as serialized bytes
    int gt_size = 0;
    const char* gt_bytes = SE_GetOSIGroundTruth(&gt_size);

    // 5. Call Python controller synchronously
    const std::size_t frame_index = controller.NextFrameIndex();

    PythonFrameData frame_data;
    frame_data.frame_id           = frame_index;
    frame_data.ground_truth_bytes = gt_bytes;
    frame_data.ground_truth_size  = gt_size;
    frame_data.waypoints          = &controller.waypoints_;
    frame_data.waypoint_index     = controller.currentWaypointIndex_;
    frame_data.lon_profile        = &lon_profile;
    frame_data.set_speed          = controller.setSpeed_;
    frame_data.current_speed      = controller.currentSpeed_;
    frame_data.dt                 = time_step;

    PythonDriverInput py_input = controller.python_bridge_->CallStep(frame_data);

    if (controller.python_bridge_->HasFatalError())
    {
        controller.FailAndStop(
            "PythonDriverController: step() failed at frame " + std::to_string(frame_index) +
            " (" + controller.python_bridge_->GetLastError() + ")");
        return;
    }

    // 6. Apply Python result to controller input
    if (!py_input.valid)
    {
        controller.FailAndStop("PythonDriverController: step() returned invalid data at frame " + std::to_string(frame_index));
        return;
    }
    else
    {
        controller.input_.throttle    = py_input.throttle;
        controller.input_.brake       = py_input.brake;
        controller.input_.steering    = py_input.steering;
        controller.input_.gear        = py_input.gear;
        for (std::size_t i = 0; i < static_cast<std::size_t>(ControllerLightSlot::COUNT); ++i)
        {
            controller.input_.lights[i] = py_input.lights.states[i];
        }
        controller.input_.engineBrake = py_input.engineBrake;
        if (!py_input.adasStates.empty())
        {
            controller.input_.adasStates = py_input.adasStates;
        }
    }

    // 7. Update vehicle physics with the Python-provided inputs
    controller.UpdateVehiclePhysics(time_step);

    // 8. Update OSI data caches
    controller.UpdateCachedPowertrain();
    controller.UpdateHostVehicleReporter();

    // 9. Sync esmini object state
    double combined_pitch = 0.0;
    double combined_roll  = 0.0;
    controller.real_vehicle_.GetCombinedAttitude(combined_pitch, combined_roll);

    if (controller.object_ && controller.gateway_)
    {
        controller.SyncGatewayObjectState(combined_pitch, combined_roll);
        controller.UpdateVehicleLights();
    }

    // 10. Base class step and refresh
    controller.scenarioengine::Controller::Step(time_step);
    controller.RefreshWaypointsOnRoutePointerChange();
}
} // namespace gt_esmini

#endif // GT_ENABLE_EMBEDDED_PYTHON
