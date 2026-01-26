#pragma once

#include "Controller.hpp"
#include "RealVehicle.hpp"
#include "UDP.hpp"
#include "GT_UDP.hpp"
#include "osi_hostvehicledata.pb.h"
#include "RoadManager.hpp"
#include <vector>

#define CONTROLLER_REAL_DRIVER_TYPE_NAME "RealDriverController"
#define DEFAULT_REAL_DRIVER_PORT         53995

namespace gt_esmini
{
    // Waypoint structure for UDP transmission
    struct WaypointData
    {
        double x;
        double y;
        double h;
        uint32_t roadId;
        double s;
        int32_t laneId;
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
        // Extract waypoints from object's route (if available)
        void ExtractWaypoints();
        // Send waypoints via UDP
        void SendWaypointsUDP();

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
    };

    scenarioengine::Controller* InstantiateControllerRealDriver(void* args);

} // namespace gt_esmini
