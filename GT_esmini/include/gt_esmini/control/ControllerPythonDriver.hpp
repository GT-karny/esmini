#pragma once

#ifdef GT_ENABLE_EMBEDDED_PYTHON

#include "Controller.hpp"
#include "gt_esmini/control/ControllerRealDriver.hpp"  // WaypointData
#include "gt_esmini/control/RealVehicle.hpp"
#include "gt_esmini/control/realdriver/LonProfilePlanner.hpp"
#include "osi_hostvehicledata.pb.h"

#include <string>
#include <vector>
#include <array>

#define CONTROLLER_PYTHON_DRIVER_TYPE_NAME "PythonDriverController"

namespace gt_esmini
{
class PythonDriverBridge;
class PythonDriverCoordinator;

enum class ControllerLightSlot : std::size_t
{
    LOW_BEAM = 0,
    HIGH_BEAM,
    LEFT_INDICATOR,
    RIGHT_INDICATOR,
    FOG,
    BRAKE,
    REVERSE,
    COUNT
};

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
        // -1: unspecified, 0: auto, 1: off, 2: on
        std::array<int, static_cast<std::size_t>(ControllerLightSlot::COUNT)> lights{
            -1, -1, -1, -1, -1, -1, -1
        };
        double engineBrake = 0.49;
        std::vector<int> adasStates;
    };

    struct LightRuntimeState
    {
        bool manual_override = false;
        bool manual_on = false;
    };

    struct RunningActionState
    {
        scenarioengine::LatLaneChangeAction* laneChange = nullptr;
        scenarioengine::LatLaneOffsetAction* laneOffset = nullptr;
        scenarioengine::FollowTrajectoryAction* followTrajectory = nullptr;
        scenarioengine::AssignRouteAction* assignRoute = nullptr;
        scenarioengine::LongDistanceAction* longDistance = nullptr;
        scenarioengine::LongSpeedProfileAction* speedProfile = nullptr;
        scenarioengine::SynchronizeAction* synchronize = nullptr;
    };

    struct ActionFlags
    {
        bool laneChanging = false;
        bool laneOffsetting = false;
        bool followingTrajectory = false;
        bool assigningRoute = false;
        bool longitudinalDistance = false;
        bool speedProfile = false;
        bool synchronize = false;
    };

    struct FrameActionContext
    {
        bool assignRoute = false;
        bool laneChange = false;
        int laneChangeTargetLane = 0;
        bool hasLaneChangeTargetLane = false;
        bool laneOffset = false;
        double laneOffsetTargetM = 0.0;
        bool hasLaneOffsetTargetM = false;
        bool followTrajectory = false;
        bool longitudinalDistance = false;
        bool speedProfile = false;
        bool synchronize = false;
    };

    void UpdateSetSpeedFromScenarioObject();
    void DetectSpeedActionTarget();
    void EvaluateScenarioActions();
    void UpdateVehiclePhysics(double timeStep);
    void UpdateCachedPowertrain();
    void UpdateHostVehicleReporter() const;
    void UpdateVehicleLights();
    void ApplyLightPatch();
    void SyncObjectPoseFromRealVehicle();
    void SyncGatewayObjectState(double combinedPitch, double combinedRoll);
    int BuildLightMaskFromExtension() const;

    void EnsureWaypointsExtracted();
    void ExtractWaypoints(const char* reason = nullptr);
    void RefreshWaypointsOnRoutePointerChange();
    void UpdateCurrentWaypointIndex();

    scenarioengine::OSCPrivateAction* GetRunningPrivateActionByType(scenarioengine::OSCAction::ActionType type);
    scenarioengine::LatLaneChangeAction* GetRunningLaneChangeAction();
    scenarioengine::LatLaneOffsetAction* GetRunningLaneOffsetAction();
    scenarioengine::LongDistanceAction* GetRunningLongDistanceAction();
    scenarioengine::LongSpeedProfileAction* GetRunningSpeedProfileAction();
    scenarioengine::FollowTrajectoryAction* GetRunningFollowTrajectoryAction();
    scenarioengine::SynchronizeAction* GetRunningSynchronizeAction();
    scenarioengine::AssignRouteAction* GetRunningAssignRouteAction();
    RunningActionState GetRunningActionState();
    static ActionFlags ToActionFlags(const RunningActionState& state);
    bool HandlePathActions(const RunningActionState& state, const ActionFlags& previousFlags, const char* phaseLabel);
    void RegenerateWaypointsForLaneChange(int targetLaneId, double transitionDuration);
    void RegenerateWaypointsForLaneOffset(double targetOffset, double transitionDistance);
    void RegenerateWaypointsForTrajectory(scenarioengine::FollowTrajectoryAction* action);
    void ProcessPendingActionEnds();

    void FailAndStop(const std::string& message);
    bool HasFatalError() const { return fatal_error_; }
    std::size_t NextFrameIndex() { return frame_index_++; }

    RealVehicle real_vehicle_;
    DriverInput input_;
    osi3::HostVehicleData cached_hvd_;
    std::vector<WaypointData> waypoints_;
    int currentWaypointIndex_ = 0;
    bool waypointsExtracted_  = false;
    std::size_t waypointGenerationVersion_ = 0;
    const roadmanager::Route* lastObservedRoute_ = nullptr;
    bool wasLaneChanging_ = false;
    bool wasLaneOffsetting_ = false;
    bool wasFollowingTrajectory_ = false;
    bool wasAssigningRoute_ = false;
    bool wasLongitudinalDistance_ = false;
    bool wasSpeedProfile_ = false;
    bool wasSynchronize_ = false;
    FrameActionContext frame_action_context_;
    double setSpeed_ = 0.0;
    double currentSpeed_ = 0.0;
    double lastWrittenGatewaySpeed_ = 0.0; // Speed written to gateway by this controller
    std::vector<const scenarioengine::OSCAction*> processedSpeedActions_; // Track SpeedActions already consumed

    struct PendingActionEnd
    {
        scenarioengine::OSCAction* action;
        double endTime;
    };
    std::vector<PendingActionEnd> pendingActionEnds_;

    PythonDriverBridge*      python_bridge_      = nullptr;
    PythonDriverCoordinator* python_coordinator_ = nullptr;
    LonProfilePlanner*       lon_profile_planner_ = nullptr;

    std::string python_script_path_;
    std::string python_class_name_ = "EmbeddedController";
    std::string python_home_;
    std::string resolved_script_path_;
    bool python_trace_enabled_ = false;
    std::string python_trace_dir_;
    bool fatal_error_ = false;
    std::size_t frame_index_ = 0;
    std::array<LightRuntimeState, static_cast<std::size_t>(ControllerLightSlot::COUNT)> light_runtime_;
};

scenarioengine::Controller* InstantiateControllerPythonDriver(void* args);

} // namespace gt_esmini

#endif // GT_ENABLE_EMBEDDED_PYTHON
