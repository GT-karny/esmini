#pragma once

#include "Controller.hpp"
#include "Action.hpp"
#include "gt_esmini/control/RealVehicle.hpp"
#include "UDP.hpp"
#include "gt_esmini/io/GT_UDP.hpp"
#include "osi_hostvehicledata.pb.h"
#include "RoadManager.hpp"
#include "gt_esmini/control/realdriver/LonProfilePlanner.hpp"
#include <vector>

namespace scenarioengine {
class OSCPrivateAction;
class LatLaneChangeAction;
class LatLaneOffsetAction;
class LongDistanceAction;
class LongSpeedProfileAction;
class FollowTrajectoryAction;
class SynchronizeAction;
class AssignRouteAction;
}

#define CONTROLLER_REAL_DRIVER_TYPE_NAME "RealDriverController"
#define DEFAULT_REAL_DRIVER_PORT         53995

namespace gt_esmini
{
    class DriverInputReceiver;
    class VehicleStateUpdater;
    class EsminiStateApplier;
    class ControlDecisionEngine;
    class DriverOutputPort;
    class LatPathPlanner;
    class RealDriverCoordinator;

    // Waypoint structure for UDP transmission
    struct WaypointData
    {
        double x;
        double y;
        double h;
        uint32_t roadId;
        double s;
        int32_t laneId;
        double laneOffset;  // Offset from lane center (meters)
    };

    class ControllerRealDriver : public scenarioengine::Controller
    {
    public:
        ControllerRealDriver(InitArgs* args);
        virtual ~ControllerRealDriver();

        void Step(double timeStep) override;
        int  Activate(const ControlActivationMode (&mode)[static_cast<unsigned int>(ControlDomains::COUNT)]) override;

        virtual const char* GetTypeName() override
        {
            return CONTROLLER_REAL_DRIVER_TYPE_NAME;
        }

        // Getters for OSI HostVehicleData (used by GT_Step)
        void GetInputsForOSI(double& throttle, double& brake, double& steering, int& gear, int& lightMask) const;
        void GetPowertrainForOSI(double& rpm, double& torque) const;
        void GetADASStates(std::vector<int>& states) const;

        // New: Get the full cached HostVehicleData (partially filled from UDP)
        const osi3::HostVehicleData& GetCachedHostVehicleData() const { return cached_hvd_; }

    private:
        friend class DriverInputReceiver;
        friend class VehicleStateUpdater;
        friend class EsminiStateApplier;
        friend class ControlDecisionEngine;
        friend class LonProfilePlanner;
        friend class DriverOutputPort;
        friend class LatPathPlanner;
        friend class RealDriverCoordinator;
#ifdef GT_ENABLE_EMBEDDED_PYTHON
        friend class PythonDriverCoordinator;
#endif

        struct RunningActionState
        {
            scenarioengine::LatLaneChangeAction* laneChange = nullptr;
            scenarioengine::LatLaneOffsetAction* laneOffset = nullptr;
            scenarioengine::FollowTrajectoryAction* followTrajectory = nullptr;
            scenarioengine::AssignRouteAction* assignRoute = nullptr;
            scenarioengine::LongDistanceAction* longDistance = nullptr;
            scenarioengine::LongSpeedProfileAction* speedProfile = nullptr;
            scenarioengine::SynchronizeAction* synchronize = nullptr;
            bool hasRunningScenarioLongAction = false;
        };

        struct ActionFlags
        {
            bool laneChanging = false;
            bool laneOffsetting = false;
            bool followingTrajectory = false;
            bool assigningRoute = false;
        };

        RunningActionState GetRunningActionState();
        void UpdateSetSpeedFromScenarioObject();
        void ReceiveLatestUdpInput();
        bool ParseDriverInputPacket(int packetSize);
        void UpdateVehiclePhysics(double timeStep);
        void MaybeSendWaypoints();
        void UpdateCachedPowertrain();
        void UpdateHostVehicleReporter() const;
        bool HandlePathActions(const RunningActionState& state, const ActionFlags& previousFlags, const char* phaseLabel);
        void SyncGatewayObjectState(double combinedPitch, double combinedRoll, bool blockSpeedUpdate);
        void SyncObjectPoseFromRealVehicle();
        void UpdateVehicleLights();
        void RefreshWaypointsOnRoutePointerChange();
        static ActionFlags ToActionFlags(const RunningActionState& state);

        // Extract waypoints from object's route (if available)
        void ExtractWaypoints();
        // Send waypoints via UDP
        void SendWaypointsUDP();
        // Get target speed from running SpeedActions
        double GetTargetSpeedFromActions(bool* hasRunningAction = nullptr);
        // Get running LaneChangeAction (or nullptr if none)
        scenarioengine::LatLaneChangeAction* GetRunningLaneChangeAction();
        // Get first running private action of a specific type
        scenarioengine::OSCPrivateAction* GetRunningPrivateActionByType(scenarioengine::OSCAction::ActionType type);
        // Additional supported actions
        scenarioengine::LatLaneOffsetAction* GetRunningLaneOffsetAction();
        scenarioengine::LongDistanceAction* GetRunningLongDistanceAction();
        scenarioengine::LongSpeedProfileAction* GetRunningSpeedProfileAction();
        scenarioengine::FollowTrajectoryAction* GetRunningFollowTrajectoryAction();
        scenarioengine::SynchronizeAction* GetRunningSynchronizeAction();
        scenarioengine::AssignRouteAction* GetRunningAssignRouteAction();
        // Regenerate waypoints with smooth sinusoidal transition to target lane
        void RegenerateWaypointsForLaneChange(int targetLaneId, double transitionDuration);
        // Regenerate waypoints for lane offset transition
        void RegenerateWaypointsForLaneOffset(double targetOffset, double transitionDistance);
        // Regenerate waypoints from FollowTrajectory action
        void RegenerateWaypointsForTrajectory(scenarioengine::FollowTrajectoryAction* action);

        RealVehicle  real_vehicle_;
        UDPServer*   udpServer_;
        int          port_;

        // UDP Client for sending target speed and waypoints
        GT_UDP_Sender* udpClient_;
        GT_UDP_Sender* waypointClient_;  // Separate client for waypoints
        std::string  clientAddr_;
        int          clientPort_;
        int          waypointPort_;      // Waypoint UDP port

        // Target speed detection (similar to ControllerACC)
        double       setSpeed_;      // Target speed from SpeedAction
        double       currentSpeed_;  // Previous speed for change detection

        // Waypoint sending (optional, for Python fallback)
        bool         sendWaypoints_;     // Property: SendWaypoints (default: false)
        std::vector<WaypointData> waypoints_;
        int          currentWaypointIndex_;
        bool         waypointsExtracted_;
        const roadmanager::Route* lastObservedRoute_ = nullptr;

        // LaneChangeAction cooperative control
        bool         wasLaneChanging_ = false;  // Track previous frame state for re-sync
        bool         wasLaneOffsetting_ = false;
        bool         wasFollowingTrajectory_ = false;
        bool         wasAssigningRoute_ = false;

        struct DriverInput
        {
            double throttle;
            double brake;
            double steering;
            int    gear = 1;
            int    lightMask = 0;
            double engineBrake;
            std::vector<int> adasStates; // Full OSI states for each function
        } input_;

        // Cached HostVehicleData from UDP
        osi3::HostVehicleData cached_hvd_;

        // Buffer for receiving UDP data
        std::vector<char> udp_buffer_;

        DriverInputReceiver* driver_input_receiver_ = nullptr;
        VehicleStateUpdater* vehicle_state_updater_ = nullptr;
        EsminiStateApplier* esmini_state_applier_ = nullptr;
        ControlDecisionEngine* control_decision_engine_ = nullptr;
        DriverOutputPort* driver_output_port_ = nullptr;
        LonProfilePlanner* lon_profile_planner_ = nullptr;
        LatPathPlanner* lat_path_planner_ = nullptr;
        RealDriverCoordinator* coordinator_ = nullptr;
    };

    scenarioengine::Controller* InstantiateControllerRealDriver(void* args);

} // namespace gt_esmini
