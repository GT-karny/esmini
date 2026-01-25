#pragma once

#include "Controller.hpp"
#include "RealVehicle.hpp"
#include "UDP.hpp"
#include "GT_UDP.hpp"
#include "osi_hostvehicledata.pb.h"

#define CONTROLLER_REAL_DRIVER_TYPE_NAME "RealDriverController"
#define DEFAULT_REAL_DRIVER_PORT         53995 

namespace gt_esmini
{
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
        RealVehicle  real_vehicle_;
        UDPServer*   udpServer_;
        int          port_;
        
        // UDP Client for sending target speed
        GT_UDP_Sender* udpClient_;
        std::string  clientAddr_;
        int          clientPort_;
        
        // Target speed detection (similar to ControllerACC)
        double       setSpeed_;      // Target speed from SpeedAction
        double       currentSpeed_;  // Previous speed for change detection
        
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
