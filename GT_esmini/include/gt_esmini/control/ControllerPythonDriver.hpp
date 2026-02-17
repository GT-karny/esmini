#pragma once

#ifdef GT_ENABLE_EMBEDDED_PYTHON

#include "Controller.hpp"
#include "gt_esmini/control/ControllerRealDriver.hpp"  // WaypointData
#include "gt_esmini/control/RealVehicle.hpp"
#include "gt_esmini/control/realdriver/LonProfilePlanner.hpp"
#include "osi_hostvehicledata.pb.h"

#include <string>
#include <vector>

#define CONTROLLER_PYTHON_DRIVER_TYPE_NAME "PythonDriverController"

namespace gt_esmini
{
class PythonDriverBridge;
class PythonDriverCoordinator;

class ControllerPythonDriver : public scenarioengine::Controller
{
public:
    ControllerPythonDriver(InitArgs* args);
    ~ControllerPythonDriver() override;

    void Step(double timeStep) override;
    int  Activate(const ControlActivationMode (&mode)[static_cast<unsigned int>(ControlDomains::COUNT)]) override;

    const char* GetTypeName() override
    {
        return CONTROLLER_PYTHON_DRIVER_TYPE_NAME;
    }

    void GetInputsForOSI(double& throttle, double& brake, double& steering, int& gear, int& lightMask) const;
    void GetPowertrainForOSI(double& rpm, double& torque) const;
    void GetADASStates(std::vector<int>& states) const;

private:
    friend class PythonDriverCoordinator;

    struct DriverInput
    {
        double throttle    = 0.0;
        double brake       = 0.0;
        double steering    = 0.0;
        int    gear        = 1;
        int    lightMask   = 0;
        double engineBrake = 0.49;
        std::vector<int> adasStates;
    };

    void UpdateSetSpeedFromScenarioObject();
    void UpdateVehiclePhysics(double timeStep);
    void UpdateCachedPowertrain();
    void UpdateHostVehicleReporter() const;
    void UpdateVehicleLights();
    void SyncObjectPoseFromRealVehicle();
    void SyncGatewayObjectState(double combinedPitch, double combinedRoll);

    void EnsureWaypointsExtracted();
    void ExtractWaypoints();
    void RefreshWaypointsOnRoutePointerChange();

    void FailAndStop(const std::string& message);
    bool HasFatalError() const { return fatal_error_; }
    std::size_t NextFrameIndex() { return frame_index_++; }

    RealVehicle real_vehicle_;
    DriverInput input_;
    osi3::HostVehicleData cached_hvd_;
    std::vector<WaypointData> waypoints_;
    int currentWaypointIndex_ = 0;
    bool waypointsExtracted_  = false;
    const roadmanager::Route* lastObservedRoute_ = nullptr;
    double setSpeed_ = 0.0;
    double currentSpeed_ = 0.0;

    PythonDriverBridge*      python_bridge_      = nullptr;
    PythonDriverCoordinator* python_coordinator_ = nullptr;
    LonProfilePlanner*       lon_profile_planner_ = nullptr;

    std::string python_script_path_;
    std::string python_class_name_ = "EmbeddedController";
    std::string python_home_;
    std::string resolved_script_path_;
    bool fatal_error_ = false;
    std::size_t frame_index_ = 0;
};

scenarioengine::Controller* InstantiateControllerPythonDriver(void* args);

} // namespace gt_esmini

#endif // GT_ENABLE_EMBEDDED_PYTHON
