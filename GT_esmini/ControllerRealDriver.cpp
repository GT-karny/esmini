#include "ControllerRealDriver.hpp"
#include <windows.h> // For GetModuleFileName
#include "logger.hpp"
#include "ScenarioGateway.hpp"
#include "Entities.hpp"
#include "ExtraEntities.hpp" // For Light Extension
#include "TerrainTracker.hpp" // For terrain tracking
#include "GT_HostVehicleReporter.hpp"

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
      udpClient_(nullptr),
      port_(DEFAULT_REAL_DRIVER_PORT),
      clientAddr_("127.0.0.1"),
      clientPort_(DEFAULT_REAL_DRIVER_PORT + 1000),  // Default: 54995
      setSpeed_(0.0),
      currentSpeed_(0.0)
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
    
    // UDP Client configuration for sending target speed
    if (args && args->properties && args->properties->ValueExists("ClientAddr"))
    {
        clientAddr_ = args->properties->GetValueStr("ClientAddr");
    }
    if (args && args->properties && args->properties->ValueExists("ClientPort"))
    {
        clientPort_ = strtol(args->properties->GetValueStr("ClientPort").c_str(), nullptr, 10);
    }
    
    // Resize buffer for OSI messages (64KB should be sufficient for HostVehicleData)
    udp_buffer_.resize(65536);
}

ControllerRealDriver::~ControllerRealDriver()
{
    if (udpServer_) delete udpServer_;
    if (udpClient_) delete udpClient_;
}

int ControllerRealDriver::Activate(const ControlActivationMode (&mode)[static_cast<unsigned int>(ControlDomains::COUNT)])
{
    LOG_INFO("RealDriverController::Activate() called");

    if (object_)
    {
        // [Logic Change] Use fixed port, do NOT add object ID.
        // This simplifies control (always target specific port 53995)
        int final_port = port_;

        if (!udpServer_ || udpServer_->GetPort() != final_port)
        {
             if (udpServer_) delete udpServer_;
             udpServer_ = new UDPServer(static_cast<unsigned short>(final_port), 1); // Asynchronous non-blocking
             
             // [DEBUG] Explicitly print port to console
             std::cout << "RealDriverController: LISTENING ON PORT " << final_port << " (FIXED PORT)" << std::endl;
             LOG_INFO("RealDriverController listening on port {}", final_port);
        }
        else
        {
             std::cout << "RealDriverController: ALREADY LISTENING ON PORT " << final_port << std::endl;
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

        // Initialize UDP Client for sending target speed
        if (!udpClient_)
        {
            udpClient_ = new GT_UDP_Sender(clientPort_, clientAddr_);
            LOG_INFO("RealDriverController: UDP client sending to {}:{}", clientAddr_, clientPort_);
        }
        
        // Initialize RealVehicle state from Object
        real_vehicle_.Reset();
        real_vehicle_.SetPos(object_->pos_.GetX(), object_->pos_.GetY(), object_->pos_.GetZ(), object_->pos_.GetH());
        real_vehicle_.SetSpeed(object_->GetSpeed());

        // If object has bounding box, set length
        real_vehicle_.SetLength(object_->boundingbox_.dimensions_.length_);

        // Initialize target speed detection
        currentSpeed_ = object_->GetSpeed();
        setSpeed_ = object_->GetSpeed();
        LOG_INFO("RealDriver: Initial target speed: {:.2f} m/s", setSpeed_);

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

    // 0. Detect target speed changes (similar to ControllerACC)
    if (abs(object_->GetSpeed() - currentSpeed_) > 1e-3)
    {
        LOG_INFO("RealDriver: New target speed detected: {:.2f} m/s (was {:.2f} m/s)", 
                 object_->GetSpeed(), setSpeed_);
        setSpeed_ = object_->GetSpeed();
    }

    // 1. Receive UDP Network Data
    if (udpServer_)
    {
        int res = 0;
        // Drain queue, get latest
        while (true)
        {
            int r = udpServer_->Receive(udp_buffer_.data(), static_cast<int>(udp_buffer_.size()));
            
            // [DEBUG] Diagnostic logging
            static int poll_counter = 0;
            // Print every 100 polling attempts (approx 1 sec), regardless of result
            if (poll_counter++ % 100 == 0) {
                 LOG_INFO("RealDriverController: Polling UDP... Res={}", r);
            }

            // [DEBUG] WSAGetLastError for diagnosis
            if (r < 0) {
#ifdef _WIN32
                int err = WSAGetLastError();
                static int last_err = 0;
                if (err != last_err || poll_counter % 500 == 0) {
                    LOG_INFO("RealDriverController: Recv failed, WSAError={}", err);
                    last_err = err;
                }
#endif
            }

            if (r > 0)
            {
                // New Packet Structure: [LightMask (4 bytes)] + [HostVehicleData]
                if (r >= 4)
                {
                    // Extract Light Mask (Little Endian int32)
                    int* maskPtr = reinterpret_cast<int*>(udp_buffer_.data());
                    input_.lightMask = *maskPtr;
                    
                    // Parse Protobuf (offset by 4 bytes)
                    if (cached_hvd_.ParseFromArray(udp_buffer_.data() + 4, r - 4))
                    {
                        // [DEBUG] Log successful parse
                         static int log_counter = 0;
                         if (log_counter++ % 50 == 0) {
                             std::cout << "RealDriverController: Packet Received (" << r << " bytes). LightMask=" << input_.lightMask << std::endl;
                             if (cached_hvd_.has_vehicle_powertrain()) {
                                 std::cout << "  - Throttle: " << cached_hvd_.vehicle_powertrain().pedal_position_acceleration() 
                                           << " Gear: " << cached_hvd_.vehicle_powertrain().gear_transmission() << std::endl;
                             }
                             if (cached_hvd_.has_vehicle_brake_system()) {
                                 std::cout << "  - Brake: " << cached_hvd_.vehicle_brake_system().pedal_position_brake() << std::endl;
                             }
                             if (cached_hvd_.has_vehicle_steering()) {
                                 std::cout << "  - Steer: " << cached_hvd_.vehicle_steering().vehicle_steering_wheel().angle() << std::endl;
                             }
                         }

                        // Extract inputs for RealVehicle Simulation
                        if (cached_hvd_.has_vehicle_powertrain())
                        {
                            input_.throttle = cached_hvd_.vehicle_powertrain().pedal_position_acceleration();
                            input_.gear = cached_hvd_.vehicle_powertrain().gear_transmission();
                        }
                        else
                        {
                            input_.throttle = 0;
                            input_.gear = 1;
                        }
                        
                        if (cached_hvd_.has_vehicle_brake_system())
                        {
                             input_.brake = cached_hvd_.vehicle_brake_system().pedal_position_brake();
                        }
                        
                        if (cached_hvd_.has_vehicle_steering() && cached_hvd_.vehicle_steering().has_vehicle_steering_wheel())
                        {
                            input_.steering = cached_hvd_.vehicle_steering().vehicle_steering_wheel().angle();
                        }
                        
                        // Engine Brake: Custom/Default
                        input_.engineBrake = 0.49; 

                        // Extract ADAS States
                        input_.adasStates.assign(24, 0); // Initialize with 0 (UNKNOWN)
                        
                        // Map standard OSI AutomatedDrivingFunction names to our internal index 0..23
                        // Simple mapper helper
                        auto mapAdasFuncToRemove = [](const std::string& name) -> int {
                             // This should match the array in GT_esminiLib.cpp
                             if (name == "BLIND_SPOT_WARNING") return 0;
                             if (name == "FORWARD_COLLISION_WARNING") return 1;
                             if (name == "LANE_DEPARTURE_WARNING") return 2;
                             if (name == "PARKING_COLLISION_WARNING") return 3;
                             if (name == "REAR_CROSS_TRAFFIC_WARNING") return 4;
                             if (name == "AUTOMATIC_EMERGENCY_BRAKING") return 5;
                             if (name == "AUTOMATIC_EMERGENCY_STEERING") return 6;
                             if (name == "REVERSE_AUTOMATIC_EMERGENCY_BRAKING") return 7;
                             if (name == "ADAPTIVE_CRUISE_CONTROL") return 8;
                             if (name == "LANE_KEEPING_ASSIST") return 9;
                             if (name == "ACTIVE_DRIVING_ASSISTANCE") return 10;
                             if (name == "BACKUP_CAMERA") return 11;
                             if (name == "SURROUND_VIEW_CAMERA") return 12;
                             if (name == "NIGHT_VISION") return 13;
                             if (name == "HEAD_UP_DISPLAY") return 14;
                             if (name == "ACTIVE_PARKING_ASSISTANCE") return 15;
                             if (name == "REMOTE_PARKING_ASSISTANCE") return 16;
                             if (name == "TRAILER_ASSISTANCE") return 17;
                             if (name == "AUTOMATIC_HIGH_BEAMS") return 18;
                             if (name == "DRIVER_MONITORING") return 19;
                             if (name == "URBAN_DRIVING") return 20;
                             if (name == "HIGHWAY_AUTOPILOT") return 21;
                             if (name == "CRUISE_CONTROL") return 22;
                             if (name == "SPEED_LIMIT_CONTROL") return 23;
                             return -1;
                        };

                        for (const auto& func : cached_hvd_.vehicle_automated_driving_function())
                        {
                            std::string lookupName;
                            // Prefer Custom Name if set, otherwise try to use Enum name if necessary
                            // In proto3, string fields are empty if not set, no has_ method.
                            if (!func.custom_name().empty()) {
                                lookupName = func.custom_name();
                            } else {
                                // Fallback or handling for standard name enum if needed
                            }
                            
                            std::transform(lookupName.begin(), lookupName.end(), lookupName.begin(), ::toupper);
                            int idx = mapAdasFuncToRemove(lookupName);
                            if (idx >= 0 && idx < 24) {
                                input_.adasStates[idx] = static_cast<int>(func.state());
                            }
                        }
                    }
                    else
                    {
                        LOG_ERROR("RealDriverController: Failed to parse HostVehicleData");
                        std::cerr << "RealDriverController: Failed to parse HostVehicleData (" << (r-4) << " bytes)" << std::endl;
                    }
                }
                else
                {
                     LOG_WARN("RealDriverController: Packet too small ({})", r);
                     std::cerr << "RealDriverController: Packet too small (" << r << " bytes)" << std::endl;
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
    double terrain_pitch = 0.0;
    double terrain_roll = 0.0;
    if (TerrainTracker::IsEnabled()) {
        terrain_pitch = object_->pos_.GetP();
        terrain_roll = object_->pos_.GetR();
    }

    // Pass to RealVehicle before UpdatePhysics
    real_vehicle_.SetTerrainAttitude(terrain_pitch, terrain_roll);

    real_vehicle_.UpdatePhysics(timeStep, input_.throttle, input_.brake, input_.steering, input_.gear);

    // Update current speed for next change detection
    currentSpeed_ = real_vehicle_.speed_;

    // Send target speed via UDP (separate packet)
    if (udpClient_)
    {
        // Packet structure: [Type: 1 byte = 1] + [setSpeed_: 8 bytes double]
        // Use pragma pack to avoid padding
#pragma pack(push, 1)
        struct TargetSpeedPacket {
            uint8_t type;
            double targetSpeed;
        } packet;
#pragma pack(pop)
        
        packet.type = 1;  // Type identifier for target speed
        packet.targetSpeed = setSpeed_;
        
        int sent = udpClient_->Send(reinterpret_cast<char*>(&packet), sizeof(packet));
        if (sent != sizeof(packet))
        {
            static int error_counter = 0;
            if (error_counter++ % 100 == 0)
            {
                LOG_WARN("RealDriver: Failed to send target speed (sent {} bytes, expected {})", sent, sizeof(packet));
            }
        }
    }

    // [DEBUG] Log throttle and speed
    static int step_counter = 0;
    if (step_counter++ % 50 == 0) {
        LOG_INFO("RealDriver: throttle={:.2f} brake={:.2f} gear={} speed={:.2f} target={:.2f}",
                 input_.throttle, input_.brake, input_.gear, real_vehicle_.speed_, setSpeed_);
    }
    
    // Inject Physics Results back into Cached HVD
    if (cached_hvd_.has_vehicle_powertrain())
    {
        // Ensure motor exists
        if (cached_hvd_.vehicle_powertrain().motor_size() == 0)
        {
            cached_hvd_.mutable_vehicle_powertrain()->add_motor();
        }
        
        auto* motor = cached_hvd_.mutable_vehicle_powertrain()->mutable_motor(0);
        motor->set_rpm(real_vehicle_.GetRPM());
        motor->set_torque(real_vehicle_.GetTorqueOutput());
    }
    else
    {
        // Create if missing
        auto* pt = cached_hvd_.mutable_vehicle_powertrain();
        auto* motor = pt->add_motor();
        motor->set_rpm(real_vehicle_.GetRPM());
        motor->set_torque(real_vehicle_.GetTorqueOutput());
    }
    
    // PASS DATA TO REPORTER
    // Assuming object_->GetId() is the vehicle ID
    if (object_)
    {
        GT_HostVehicleReporter::Instance().SetBaseHostVehicleData(object_->GetId(), cached_hvd_);
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
        // Note: Using stored h instead of object_->pos_.GetH() to ensure consistency with real_vehicle_ state
        
        double w_dx = dx * std::cos(h) - dy * std::sin(h);
        double w_dy = dx * std::sin(h) + dy * std::cos(h);
        
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
                
                // Manual Lights from UDP (Bit Mapping)
                // Bit 0: Low Beam
                set_light(VehicleLightType::LOW_BEAM,      (mask & 1));
                // Bit 1: High Beam
                set_light(VehicleLightType::HIGH_BEAM,     (mask & 2));
                // Bit 2: Indicator Left
                set_light(VehicleLightType::INDICATOR_LEFT,(mask & 4));
                // Bit 3: Indicator Right
                set_light(VehicleLightType::INDICATOR_RIGHT,(mask & 8));
                
                // Bit 4: Fog Front
                // Bit 5: Fog Rear
                // Mapped to FOG_LIGHTS (General) and specific if available in enum
                // Checking GT_esminiLib.hpp/VehicleLightExtension definition:
                // Typically FOG_LIGHTS is generic. Let's use it for Front.
                // If FOG_LIGHTS_REAR exists, use it.
                // Assuming VehicleLightType matches standard esmini/OpenDRIVE types + extensions
                set_light(VehicleLightType::FOG_LIGHTS,      (mask & 16)); // Front
                // set_light(VehicleLightType::FOG_LIGHTS_REAR, (mask & 32)); // Check if enum exists? 
                
                // Bit 8: License Plate
                // set_light(VehicleLightType::??? ); // Need to check if available internal type
                
                // Auto Lights (Logic)
                // Brake Light (Auto from Brake Input)
                set_light(VehicleLightType::BRAKE_LIGHTS, (input_.brake > 0.05)); // Threshold
                
                // Reverse Light (Auto from Gear)
                set_light(VehicleLightType::REVERSING_LIGHTS, (input_.gear == -1));
            }
        }
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

void ControllerRealDriver::GetADASStates(std::vector<int>& states) const
{
    states = input_.adasStates;
}

} // namespace gt_esmini
