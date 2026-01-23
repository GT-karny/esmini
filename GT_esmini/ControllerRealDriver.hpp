#pragma once

#include "Controller.hpp"
#include "RealVehicle.hpp"
#include "UDP.hpp"

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
        void GetADASForOSI(unsigned int& enabledMask, unsigned int& availableMask) const;

    private:
        RealVehicle  real_vehicle_;
        UDPServer*   udpServer_;
        int          port_;
        
        struct DriverInput
        {
            double throttle;
            double brake;
            double steering;
            int    gear = 1;
            int    lightMask = 0;
            double engineBrake;
            unsigned int adasEnabledMask = 0;   // ADAS enabled state bitmask
            unsigned int adasAvailableMask = 0; // ADAS available state bitmask
        } input_;

        // Parsing helper for UDP packet (reusing structure from UDPDriverController conceptually)
        // We will assume same packet format: 
        /*
        typedef struct {
            double throttle;       // range [0, 1]
            double brake;          // range [0, 1]
            double steeringAngle;  // range [-pi/2, pi/2]
        } DMMSGDriverInput;
        */
       // But waiting for full packet definition... let's just use raw struct here for simplicity
        #pragma pack(push, 1)
        struct UDPPacket {
            unsigned int version;       // Version 1 = original, Version 2 = with ADAS
            unsigned int inputMode;
            unsigned int objectId;
            unsigned int frameNumber;
            double throttle;
            double brake;
            double steeringAngle;       // Negative = Right in python script?
            double gear;                // -1, 0, 1
            unsigned int lightMask;     // Bitmask for lights
            double engineBrake;
            // Version 2+ fields:
            unsigned int adasEnabledMask;   // ADAS enabled state bitmask (OSI compliant)
            unsigned int adasAvailableMask; // ADAS available state bitmask
        };
        #pragma pack(pop)
    };

    scenarioengine::Controller* InstantiateControllerRealDriver(void* args);

} // namespace gt_esmini
