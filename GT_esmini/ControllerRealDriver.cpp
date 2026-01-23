#include "ControllerRealDriver.hpp"
#include <windows.h> // For GetModuleFileName
#include "logger.hpp"
#include "ScenarioGateway.hpp"
#include "Entities.hpp"
#include "ExtraEntities.hpp" // For Light Extension
#include "TerrainTracker.hpp" // For terrain tracking

namespace gt_esmini
{

scenarioengine::Controller* InstantiateControllerRealDriver(void* args)
{
    scenarioengine::Controller::InitArgs* initArgs = static_cast<scenarioengine::Controller::InitArgs*>(args);
    return new ControllerRealDriver(initArgs);
}

// Helper to get directory of current module/executable
std::string GetCurrentModuleDirectory()
{
    char buffer[MAX_PATH];
    // Get path of current process executable
    // If we wanted the DLL path specifically (if this code is in a DLL), we would need the HMODULE.
    // NULL gets the path of the exe (e.g. GT_Sim.exe or Python.exe)
    if (GetModuleFileNameA(NULL, buffer, MAX_PATH) != 0)
    {
        std::string path(buffer);
        size_t last_slash = path.find_last_of("\\/");
        if (last_slash != std::string::npos)
        {
            return path.substr(0, last_slash);
        }
    }
    return ".";
}

ControllerRealDriver::ControllerRealDriver(InitArgs* args)
    : Controller(args),
      udpServer_(nullptr),
      port_(DEFAULT_REAL_DRIVER_PORT)
{
    // Check if port is overridden in parameters
    if (args && args->properties && args->properties->ValueExists("BasePort"))
    {
        port_ = strtol(args->properties->GetValueStr("BasePort").c_str(), nullptr, 10);
    }
    
    // Also check "Port" parameter which might be an offset or absolute
    if (args && args->properties && args->properties->ValueExists("Port"))
    {
         // int p = strtol(args->properties->GetValueStr("Port").c_str(), nullptr, 10);
         // Storing explicit port for now if needed.
    }
}

ControllerRealDriver::~ControllerRealDriver()
{
    if (udpServer_) delete udpServer_;
}

int ControllerRealDriver::Activate(const ControlActivationMode (&mode)[static_cast<unsigned int>(ControlDomains::COUNT)])
{
    LOG_INFO("RealDriverController::Activate() called");

    if (object_)
    {
        // Calculate port: BasePort + Object ID (simple logic)
        // Or read from params if specific
        int final_port = port_ + object_->GetId();

        if (!udpServer_ || udpServer_->GetPort() != final_port)
        {
             if (udpServer_) delete udpServer_;
             udpServer_ = new UDPServer(static_cast<unsigned short>(final_port), 1); // Asynchronous non-blocking
             LOG_INFO("RealDriverController listening on port {}", final_port);
        }
        else
        {
             LOG_INFO("RealDriverController already listening on port {}", final_port);
        }

        // Register VehicleLightExtension for light state management
        auto* vehicle = dynamic_cast<scenarioengine::Vehicle*>(object_);
        LOG_INFO("RealDriverController: Vehicle cast result: {}", (vehicle ? "SUCCESS" : "FAILED"));

        if (vehicle)
        {
            auto* ext = VehicleExtensionManager::Instance().GetExtension(vehicle);
            LOG_INFO("RealDriverController: GetExtension result: {}", (ext ? "ALREADY EXISTS" : "NULL - will create"));

            if (!ext)
            {
                ext = new VehicleLightExtension(vehicle);
                VehicleExtensionManager::Instance().RegisterExtension(vehicle, ext);
                LOG_INFO("RealDriverController: Registered VehicleLightExtension for vehicle ID {}", vehicle->GetId());
            }
            else
            {
                LOG_INFO("RealDriverController: VehicleLightExtension already exists for vehicle ID {}", vehicle->GetId());
            }
        }
        else
        {
            LOG_WARN("RealDriverController: Failed to cast object to Vehicle type");
        }

        // Initialize RealVehicle state from Object
        real_vehicle_.Reset();
        real_vehicle_.SetPos(object_->pos_.GetX(), object_->pos_.GetY(), object_->pos_.GetZ(), object_->pos_.GetH());
        real_vehicle_.SetSpeed(object_->GetSpeed());

        // If object has bounding box, set length
        real_vehicle_.SetLength(object_->boundingbox_.dimensions_.length_);

        // Tuning: Load External Param File
        // Construct absolute path based on executable location
        std::string exeDir = GetCurrentModuleDirectory();
        std::string paramFile = exeDir + "/real_vehicle_params.json";

        // Log for debugging
        LOG_INFO("RealDriver: Loading params from {}", paramFile);

        real_vehicle_.LoadParameters(paramFile);
    }

    return Controller::Activate(mode);
}

void ControllerRealDriver::Step(double timeStep)
{
    // Note: TerrainTracker::UpdateAllVehicleTerrain() is now called from GT_Step()
    // to avoid dependency issues with ScenarioEngine access

    // 1. Receive UDP Network Data
    if (udpServer_)
    {
        UDPPacket packet;
        int res = 0;
        // Drain queue, get latest
        while (true)
        {
            int r = udpServer_->Receive(reinterpret_cast<char*>(&packet), sizeof(packet));
            
            // [DEBUG] Diagnostic logging
            static int poll_counter = 0;
            if (poll_counter++ % 100 == 0) {
                 LOG_INFO("RealDriverController: Polling UDP... Res={}", r);
            }

            if (r > 0)
            {
                // [DEBUG] Log received packet size
                LOG_INFO("RealDriverController: Received UDP packet, size={}, version={}", r, packet.version);

                // Basic validation: check size or version if strictly needed
                // For now assumes matching struct
                input_.throttle = packet.throttle;
                input_.brake = packet.brake;
                input_.steering = packet.steeringAngle; // Python sends -wheel_angle
                input_.gear = static_cast<int>(packet.gear); // Double to Int conversion
                input_.lightMask = static_cast<int>(packet.lightMask);
                input_.engineBrake = packet.engineBrake;

                // Version 2+ fields: ADAS state (OSI compliant bitmasks)
                if (packet.version >= 2)
                {
                    input_.adasEnabledMask = packet.adasEnabledMask;
                    input_.adasAvailableMask = packet.adasAvailableMask;
                }
                else
                {
                    // Default for version 1 packets
                    input_.adasEnabledMask = 0;
                    input_.adasAvailableMask = 0;
                }

                res = r;
            }
            else
            {
                break;
            }
        }
    }

    // 2. Update Physics
    real_vehicle_.SetEngineBrakeFactor(input_.engineBrake);

    // [GT_MOD] Read terrain attitude from Object (set by TerrainTracker)
    // NOTE: Only use object_->pos_ values if TerrainTracker is enabled.
    // If disabled, object_->pos_.GetP()/GetR() returns the PREVIOUS frame's combined
    // pitch/roll (set by us), which causes a feedback loop and runaway rotation.
    double terrain_pitch = 0.0;
    double terrain_roll = 0.0;
    if (TerrainTracker::IsEnabled()) {
        terrain_pitch = object_->pos_.GetP();
        terrain_roll = object_->pos_.GetR();
    }

    // Pass to RealVehicle before UpdatePhysics
    real_vehicle_.SetTerrainAttitude(terrain_pitch, terrain_roll);

    real_vehicle_.UpdatePhysics(timeStep, input_.throttle, input_.brake, input_.steering, input_.gear);

    // [DEBUG] Log throttle and speed
    static int step_counter = 0;
    if (step_counter++ % 50 == 0) {
        LOG_INFO("RealDriver: throttle={:.2f} brake={:.2f} gear={} speed={:.2f}",
                 input_.throttle, input_.brake, input_.gear, real_vehicle_.speed_);
    }

    // [GT_MOD] Get combined attitude (terrain + dynamic) and update Object
    double combined_pitch, combined_roll;
    real_vehicle_.GetCombinedAttitude(combined_pitch, combined_roll);

    // 3. Update Simulation Object
    if (object_ && gateway_)
    {
        // Calculate visual/physical pivot offset
        double dx, dy, dz;
        real_vehicle_.GetBodyPositionOffset(dx, dy, dz);
        
        // Rotate offset by Heading to match world frame alignment?
        // Offsets dx, dy from GetBodyPositionOffset are in "Vehicle Frame" (X-forward, Y-left).
        // We need to rotate them by Heading to get World Offsets.
        double h = real_vehicle_.heading_;
        double w_dx = dx * std::cos(h) - dy * std::sin(h);
        double w_dy = dx * std::sin(h) + dy * std::cos(h);
        // dz is vertical, no heading rotation needed.
        
        // Update Position & Heading
        gateway_->updateObjectWorldPosXYH(object_->id_, 0.0, 
            real_vehicle_.posX_ + w_dx, 
            real_vehicle_.posY_ + w_dy, 
            real_vehicle_.heading_);
            
        // Update Speed
        gateway_->updateObjectSpeed(object_->id_, 0.0, real_vehicle_.speed_);
        
        // Update Wheel Angle (for visualization)
        gateway_->updateObjectWheelAngle(object_->id_, 0.0, real_vehicle_.wheelAngle_);
        
        // Update Pitch & Roll (Extended Physics with Terrain!)
        // Apply Z update with pivot offset
        gateway_->updateObjectWorldPos(object_->id_, 0.0,
            real_vehicle_.posX_ + w_dx,
            real_vehicle_.posY_ + w_dy,
            real_vehicle_.posZ_ + dz, // Apply pivot vertical offset
            real_vehicle_.heading_,
            combined_pitch,  // Terrain + Dynamic
            combined_roll    // Terrain + Dynamic
        );
        
        // 4. Update Lights (Extensions)
        auto* vehicle = dynamic_cast<scenarioengine::Vehicle*>(object_);
        if (vehicle)
        {
            auto* ext = VehicleExtensionManager::Instance().GetExtension(vehicle);
            if (ext)
            {
                // Helper lambda
                auto set_light = [&](VehicleLightType type, bool on) {
                    LightState s;
                    s.mode = on ? LightState::Mode::ON : LightState::Mode::OFF;
                    ext->SetLightState(type, s);
                };
                
                int mask = input_.lightMask;
                
                // Manual Lights from UDP
                set_light(VehicleLightType::LOW_BEAM,      (mask & 1));
                set_light(VehicleLightType::HIGH_BEAM,     (mask & 2));
                set_light(VehicleLightType::INDICATOR_LEFT,(mask & 4));
                set_light(VehicleLightType::INDICATOR_RIGHT,(mask & 8));
                set_light(VehicleLightType::WARNING_LIGHTS,(mask & 16));
                set_light(VehicleLightType::FOG_LIGHTS_FRONT,(mask & 32));
                set_light(VehicleLightType::FOG_LIGHTS_REAR, (mask & 64));
                
                // Auto Lights (Logic)
                // Brake Light
                set_light(VehicleLightType::BRAKE_LIGHTS, (input_.brake > 0.05)); // Threshold
                
                // Reverse Light
                set_light(VehicleLightType::REVERSING_LIGHTS, (input_.gear == -1));
            }
        }

        
        // Hack: To allow terrain following for Z, we should probably read Z back from object
        // after esmini might have snapped it? Or RealVehicle needs to know Z.
        // For simple rollout: Let's assume flat ground or rely on slight Z updates if any.
        // Better: Read current Z from object state (which might be updated by road query if enabled?)
        // Actually Controller overrides everything.
        // To do terrain following properly, we need to query road/terrain Z at (X, Y).
        // For MVP, we pass Z=0 or current Z.
    }

    Controller::Step(timeStep);
}

// Getter for input data (used by GT_Step for HostVehicleData)
void ControllerRealDriver::GetInputsForOSI(double& throttle, double& brake, double& steering, int& gear, int& lightMask) const
{
    throttle = input_.throttle;
    brake = input_.brake;
    steering = input_.steering;
    gear = input_.gear;
    lightMask = input_.lightMask;
}

void ControllerRealDriver::GetPowertrainForOSI(double& rpm, double& torque) const
{
    rpm = real_vehicle_.GetRPM();
    torque = real_vehicle_.GetTorqueOutput();
}

void ControllerRealDriver::GetADASForOSI(unsigned int& enabledMask, unsigned int& availableMask) const
{
    enabledMask = input_.adasEnabledMask;
    availableMask = input_.adasAvailableMask;
}

} // namespace gt_esmini
