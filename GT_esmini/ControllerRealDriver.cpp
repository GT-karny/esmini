#include "ControllerRealDriver.hpp"
#include <windows.h> // For GetModuleFileName
#include <cmath>     // For std::sqrt, std::atan2, M_PI
#include "logger.hpp"
#include "ScenarioGateway.hpp"
#include "Entities.hpp"
#include "ExtraEntities.hpp" // For Light Extension
#include "TerrainTracker.hpp" // For terrain tracking
#include "GT_HostVehicleReporter.hpp"
#include "Storyboard.hpp"      // For Event
#include "OSCPrivateAction.hpp" // For LongSpeedAction
#include "Action.hpp"          // For OSCAction::ActionType

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
      waypointClient_(nullptr),
      port_(DEFAULT_REAL_DRIVER_PORT),
      clientAddr_("127.0.0.1"),
      clientPort_(DEFAULT_REAL_DRIVER_PORT + 1000),  // Default: 54995
      waypointPort_(DEFAULT_REAL_DRIVER_PORT + 1001), // Default: 54996
      setSpeed_(0.0),
      currentSpeed_(0.0),
      sendWaypoints_(false),
      currentWaypointIndex_(0),
      waypointsExtracted_(false)
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

    // Optional: Waypoint sending configuration
    if (args && args->properties && args->properties->ValueExists("SendWaypoints"))
    {
        std::string val = args->properties->GetValueStr("SendWaypoints");
        sendWaypoints_ = (val == "true" || val == "1" || val == "True");
    }
    if (args && args->properties && args->properties->ValueExists("WaypointPort"))
    {
        waypointPort_ = strtol(args->properties->GetValueStr("WaypointPort").c_str(), nullptr, 10);
    }

    // Resize buffer for OSI messages (64KB should be sufficient for HostVehicleData)
    udp_buffer_.resize(65536);

    // [GT_MOD] FIX: Set default mode to ADDITIVE like ControllerACC
    // In ADDITIVE mode, SpeedActions from the scenario are applied to object_->speed_
    // In OVERRIDE mode (default), actions are blocked and the controller has full control
    // We need ADDITIVE to detect red light stop actions from scenarios
    if (args && args->properties && !args->properties->ValueExists("mode"))
    {
        mode_ = ControlOperationMode::MODE_ADDITIVE;
    }
}

ControllerRealDriver::~ControllerRealDriver()
{
    if (udpServer_) delete udpServer_;
    if (udpClient_) delete udpClient_;
    if (waypointClient_) delete waypointClient_;
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

        // Initialize UDP Client for sending waypoints (optional)
        if (sendWaypoints_ && !waypointClient_)
        {
            waypointClient_ = new GT_UDP_Sender(waypointPort_, clientAddr_);
            LOG_INFO("RealDriverController: Waypoint UDP client sending to {}:{}", clientAddr_, waypointPort_);
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

double ControllerRealDriver::GetTargetSpeedFromActions(bool* hasRunningAction)
{
    double targetSpeed = setSpeed_;  // Default is current set value
    bool found = false;

    if (!object_)
    {
        if (hasRunningAction) *hasRunningAction = false;
        return targetSpeed;
    }

    // 1. Search initActions_ for running LongSpeedAction
    for (auto* action : object_->initActions_)
    {
        if (action->action_type_ == scenarioengine::OSCAction::ActionType::LONG_SPEED &&
            action->GetCurrentState() == scenarioengine::StoryBoardElement::State::RUNNING)
        {
            auto* speedAction = static_cast<scenarioengine::LongSpeedAction*>(action);
            if (speedAction->target_)
            {
                found = true;
                if (speedAction->target_->type_ == scenarioengine::LongSpeedAction::Target::TargetType::ABSOLUTE_SPEED)
                {
                    targetSpeed = speedAction->target_->value_;
                }
                else  // RELATIVE_SPEED
                {
                    targetSpeed = object_->GetSpeed() + speedAction->target_->value_;
                }
            }
        }
    }

    // 2. Search objectEvents_ for running LongSpeedAction
    for (auto* event : object_->objectEvents_)
    {
        for (auto* action : event->action_)
        {
            if (action->GetBaseType() == scenarioengine::OSCAction::BaseType::PRIVATE)
            {
                auto* pa = static_cast<scenarioengine::OSCPrivateAction*>(action);
                if (pa->action_type_ == scenarioengine::OSCAction::ActionType::LONG_SPEED &&
                    pa->GetCurrentState() == scenarioengine::StoryBoardElement::State::RUNNING)
                {
                    auto* speedAction = static_cast<scenarioengine::LongSpeedAction*>(pa);
                    if (speedAction->target_)
                    {
                        found = true;
                        if (speedAction->target_->type_ == scenarioengine::LongSpeedAction::Target::TargetType::ABSOLUTE_SPEED)
                        {
                            targetSpeed = speedAction->target_->value_;
                        }
                        else
                        {
                            targetSpeed = object_->GetSpeed() + speedAction->target_->value_;
                        }
                    }
                }
            }
        }
    }

    if (hasRunningAction) *hasRunningAction = found;
    return targetSpeed;
}

scenarioengine::LatLaneChangeAction* ControllerRealDriver::GetRunningLaneChangeAction()
{
    if (!object_) return nullptr;

    // 1. Search initActions_ for running LaneChangeAction
    for (auto* action : object_->initActions_)
    {
        if (action->action_type_ == scenarioengine::OSCAction::ActionType::LAT_LANE_CHANGE &&
            action->GetCurrentState() == scenarioengine::StoryBoardElement::State::RUNNING)
        {
            return static_cast<scenarioengine::LatLaneChangeAction*>(action);
        }
    }

    // 2. Search objectEvents_ for running LaneChangeAction
    for (auto* event : object_->objectEvents_)
    {
        for (auto* action : event->action_)
        {
            if (action->GetBaseType() == scenarioengine::OSCAction::BaseType::PRIVATE)
            {
                auto* pa = static_cast<scenarioengine::OSCPrivateAction*>(action);
                if (pa->action_type_ == scenarioengine::OSCAction::ActionType::LAT_LANE_CHANGE &&
                    pa->GetCurrentState() == scenarioengine::StoryBoardElement::State::RUNNING)
                {
                    return static_cast<scenarioengine::LatLaneChangeAction*>(pa);
                }
            }
        }
    }
    return nullptr;
}

void ControllerRealDriver::Step(double timeStep)
{
    // Note: TerrainTracker::UpdateAllVehicleTerrain() is now called from GT_Step()
    // to avoid dependency issues with ScenarioEngine access

    // 0. Detect target speed changes from SpeedActions
    // [GT_MOD] FIX: Check for RUNNING SpeedActions to conditionally skip gateway speed overwrite.
    // When a SpeedAction with dynamics (linear ramp) is RUNNING, we must NOT overwrite
    // object_->speed_ via gateway, otherwise the SpeedAction's ramp cannot advance properly
    // (feedback loop: controller resets speed to 0 each frame, SpeedAction can only produce tiny increments).
    bool hasRunningSpeedAction = false;
    GetTargetSpeedFromActions(&hasRunningSpeedAction);

    // [GT_MOD] Check for RUNNING LaneChangeAction to detect lane change transitions
    // and regenerate waypoints for smooth target transition.
    auto* runningLaneChangeAction = GetRunningLaneChangeAction();
    bool hasRunningLaneChange = (runningLaneChangeAction != nullptr);

    // [DIAG] LC 前後のみ詳細ログ出力
    if (wasLaneChanging_ || hasRunningLaneChange) {
        LOG_INFO("[DIAG] LC_STATE wasLC={} hasLC={}", wasLaneChanging_, hasRunningLaneChange);
        LOG_INFO("[DIAG] object_pos  x={:.3f} y={:.3f} h={:.4f} lane={} s={:.2f}",
                 object_->pos_.GetX(), object_->pos_.GetY(), object_->pos_.GetH(),
                 object_->pos_.GetLaneId(), object_->pos_.GetS());
        LOG_INFO("[DIAG] real_vehicle x={:.3f} y={:.3f} h={:.4f} speed={:.2f}",
                 real_vehicle_.posX_, real_vehicle_.posY_, real_vehicle_.heading_, real_vehicle_.speed_);
        LOG_INFO("[DIAG] steering_in={:.4f} wheelAngle={:.4f}",
                 input_.steering, real_vehicle_.wheelAngle_);
    }

    double objectSpeed = object_->GetSpeed();
    if (abs(objectSpeed - currentSpeed_) > 1e-3)
    {
        LOG_INFO("RealDriver: Detected speed change from scenario: {:.2f} -> {:.2f} m/s",
                 currentSpeed_, objectSpeed);
        setSpeed_ = objectSpeed;
        currentSpeed_ = objectSpeed;
    }

    // 1. Receive UDP Network Data
    if (udpServer_)
    {
        int res = 0;
        // Drain queue, get latest
        while (true)
        {
            int r = udpServer_->Receive(udp_buffer_.data(), static_cast<int>(udp_buffer_.size()));
            
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

        // [DEBUG] Log target speed being sent every 50 frames
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

    // Send waypoints via UDP (optional, for Python fallback)
    if (sendWaypoints_)
    {
        // Extract waypoints on first step
        if (!waypointsExtracted_)
        {
            ExtractWaypoints();
            waypointsExtracted_ = true;
        }

        // Send waypoints periodically
        SendWaypointsUDP();
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
        // [GT_MOD] Detect lane change start → regenerate waypoints with smooth transition.
        // Python steering priority: real_vehicle_ always drives visible position,
        // waypoints provide smooth target for Python to steer towards.
        if (!wasLaneChanging_ && hasRunningLaneChange)
        {
            if (runningLaneChangeAction && runningLaneChangeAction->target_)
            {
                int targetLaneId = runningLaneChangeAction->target_->value_;
                double duration  = runningLaneChangeAction->transition_.GetParamTargetVal();
                LOG_INFO("RealDriver: LaneChange starting, target lane={}, duration={:.1f}s",
                         targetLaneId, duration);
                RegenerateWaypointsForLaneChange(targetLaneId, duration);
            }
        }
        wasLaneChanging_ = hasRunningLaneChange;

        // --- Always write real_vehicle_ to gateway (Python steering priority) ---
        // Calculate visual/physical pivot offset
        double dx, dy, dz;
        real_vehicle_.GetBodyPositionOffset(dx, dy, dz);

        // Rotate offset by Heading to match world frame alignment.
        double h = real_vehicle_.heading_;
        double w_dx = dx * std::cos(h) - dy * std::sin(h);
        double w_dy = dx * std::sin(h) + dy * std::cos(h);

        // Update Position & Heading
        gateway_->updateObjectWorldPosXYH(object_->id_, 0.0,
            real_vehicle_.posX_ + w_dx,
            real_vehicle_.posY_ + w_dy,
            real_vehicle_.heading_);

        // [DIAG] gateway 書き込み値（LC中のみ）
        if (wasLaneChanging_ || hasRunningLaneChange) {
            LOG_INFO("[DIAG] GW_WRITE x={:.3f} y={:.3f} h={:.4f}",
                     real_vehicle_.posX_ + w_dx, real_vehicle_.posY_ + w_dy, real_vehicle_.heading_);
        }

        // Update Speed
        // [GT_MOD] FIX: Skip gateway speed overwrite when a SpeedAction with dynamics is RUNNING.
        if (!hasRunningSpeedAction)
        {
            gateway_->updateObjectSpeed(object_->id_, 0.0, real_vehicle_.speed_);
        }

        // Update Wheel Angle (for visualization)
        gateway_->updateObjectWheelAngle(object_->id_, 0.0, real_vehicle_.wheelAngle_);

        // Update Pitch & Roll (Extended Physics with Terrain!)
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

void ControllerRealDriver::ExtractWaypoints()
{
    waypoints_.clear();
    currentWaypointIndex_ = 0;

    if (!object_)
    {
        LOG_WARN("RealDriver: No object to extract waypoints from");
        return;
    }

    // Try to get route from object's assigned route
    roadmanager::Route* route = object_->pos_.GetRoute();
    if (!route)
    {
        // [GT_MOD] Fallback: generate waypoints by stepping forward along the road
        // using MoveAlongS(), which automatically follows successor links and junctions.
        LOG_INFO("RealDriver: No route assigned, generating fallback waypoints by road-following");

        roadmanager::Position pos = object_->pos_;
        double step = 5.0;        // 5m intervals
        double total_dist = 500.0; // Generate for 500m ahead

        for (double d = 0; d < total_dist; d += step)
        {
            WaypointData data;
            data.x = pos.GetX();
            data.y = pos.GetY();
            data.h = pos.GetH();
            data.roadId = static_cast<uint32_t>(pos.GetTrackId());
            data.s = pos.GetS();
            data.laneId = pos.GetLaneId();
            data.laneOffset = pos.GetOffset();
            waypoints_.push_back(data);

            // Advance along road (follows successor links and junctions automatically)
            roadmanager::Position::ReturnCode rc = pos.MoveAlongS(step);
            if (static_cast<int>(rc) < 0)
            {
                LOG_INFO("RealDriver: Road-following stopped at d={:.1f}m (rc={})", d, static_cast<int>(rc));
                break;
            }
        }

        LOG_INFO("RealDriver: Generated {} fallback waypoints by road-following", waypoints_.size());

        // Debug: Log first and last waypoints
        if (!waypoints_.empty())
        {
            auto& first = waypoints_.front();
            auto& last = waypoints_.back();
            LOG_INFO("  First WP: x={:.2f}, y={:.2f}, roadId={}, s={:.2f}, laneId={}",
                     first.x, first.y, first.roadId, first.s, first.laneId);
            LOG_INFO("  Last  WP: x={:.2f}, y={:.2f}, roadId={}, s={:.2f}, laneId={}",
                     last.x, last.y, last.roadId, last.s, last.laneId);
        }
        return;
    }

    // Get all waypoints from the route
    const std::vector<roadmanager::Position>& routeWaypoints = route->all_waypoints_;

    if (routeWaypoints.empty())
    {
        LOG_INFO("RealDriver: Route has no waypoints");
        return;
    }

    // Convert to WaypointData format
    for (const auto& wp : routeWaypoints)
    {
        WaypointData data;
        data.x = wp.GetX();
        data.y = wp.GetY();
        data.h = wp.GetH();
        data.roadId = static_cast<uint32_t>(wp.GetTrackId());
        data.s = wp.GetS();
        data.laneId = wp.GetLaneId();
        data.laneOffset = wp.GetOffset();  // Extract lane offset from lane center
        waypoints_.push_back(data);
    }

    LOG_INFO("RealDriver: Extracted {} waypoints from route", waypoints_.size());

    // Debug: Log each waypoint's details
    for (size_t i = 0; i < waypoints_.size(); ++i)
    {
        LOG_INFO("  WP[{}]: x={:.2f}, y={:.2f}, h={:.2f}, roadId={}, s={:.2f}, laneId={}",
                 i, waypoints_[i].x, waypoints_[i].y, waypoints_[i].h,
                 waypoints_[i].roadId, waypoints_[i].s, waypoints_[i].laneId);
    }
}

void ControllerRealDriver::SendWaypointsUDP()
{
    if (!waypointClient_ || waypoints_.empty())
    {
        return;
    }

    // Update current waypoint index based on vehicle position using distance-based tracking
    if (object_ && !waypoints_.empty())
    {
        // [GT_MOD] Use real_vehicle_ position, NOT object_->pos_.
        // object_->pos_ is overwritten by LaneChangeAction during StoryBoard.Step(),
        // so it contains the action trajectory, not the actual driven position.
        double vehicleX = real_vehicle_.posX_;
        double vehicleY = real_vehicle_.posY_;
        double vehicleH = real_vehicle_.heading_;

        // Find current waypoint (first waypoint ahead of vehicle)
        for (size_t i = currentWaypointIndex_; i < waypoints_.size(); ++i)
        {
            // Calculate distance to waypoint
            double dx = waypoints_[i].x - vehicleX;
            double dy = waypoints_[i].y - vehicleY;
            double dist = std::sqrt(dx * dx + dy * dy);

            // Check if waypoint is close enough to consider
            if (dist < 10.0)  // Within 10m
            {
                // Check if waypoint is behind us by comparing heading
                double headingToWp = std::atan2(dy, dx);
                double angleDiff = headingToWp - vehicleH;

                // Normalize angle to [-PI, PI]
                while (angleDiff > M_PI) angleDiff -= 2 * M_PI;
                while (angleDiff < -M_PI) angleDiff += 2 * M_PI;

                // If waypoint is more than 90 degrees behind us, it's passed
                if (std::abs(angleDiff) > M_PI / 2)
                {
                    // Waypoint is behind us, advance to next
                    currentWaypointIndex_ = static_cast<int>(i) + 1;
                    continue;
                }
            }

            // Found a valid waypoint ahead
            currentWaypointIndex_ = static_cast<int>(i);
            break;
        }

        // Clamp index to valid range
        if (currentWaypointIndex_ >= static_cast<int>(waypoints_.size()))
        {
            currentWaypointIndex_ = static_cast<int>(waypoints_.size()) - 1;
        }

        // [DIAG] LC 中のウェイポイントインデックス追跡
        if (wasLaneChanging_) {
            int idx = currentWaypointIndex_;
            if (idx >= 0 && idx < static_cast<int>(waypoints_.size())) {
                LOG_INFO("[DIAG] WP idx={}/{} wp_xy=({:.2f},{:.2f}) vehicle_xy=({:.2f},{:.2f}) dist={:.2f}",
                         idx, static_cast<int>(waypoints_.size()),
                         waypoints_[idx].x, waypoints_[idx].y,
                         vehicleX, vehicleY,
                         std::sqrt((waypoints_[idx].x - vehicleX) * (waypoints_[idx].x - vehicleX) +
                                   (waypoints_[idx].y - vehicleY) * (waypoints_[idx].y - vehicleY)));
            }
        }
    }

    // Packet structure:
    // [Type: 1 byte = 2] + [CurrentIndex: 4 bytes] + [Count: 4 bytes] + [Waypoints...]
    // Each waypoint: [x: 8][y: 8][h: 8][roadId: 4][s: 8][laneId: 4][laneOffset: 8] = 48 bytes

#pragma pack(push, 1)
    struct WaypointPacketHeader {
        uint8_t type;
        uint32_t currentIndex;
        uint32_t count;
    };
#pragma pack(pop)

    size_t headerSize = sizeof(WaypointPacketHeader);
    size_t waypointSize = sizeof(WaypointData);
    size_t totalSize = headerSize + waypoints_.size() * waypointSize;

    // Allocate buffer
    std::vector<char> buffer(totalSize);

    // Fill header
    WaypointPacketHeader* header = reinterpret_cast<WaypointPacketHeader*>(buffer.data());
    header->type = 2;  // Type identifier for waypoints
    header->currentIndex = static_cast<uint32_t>(currentWaypointIndex_);
    header->count = static_cast<uint32_t>(waypoints_.size());

    // Copy waypoints
    memcpy(buffer.data() + headerSize, waypoints_.data(), waypoints_.size() * waypointSize);



    // Send
    int sent = waypointClient_->Send(buffer.data(), static_cast<int>(totalSize));
    if (sent != static_cast<int>(totalSize))
    {
        static int error_counter = 0;
        if (error_counter++ % 100 == 0)
        {
            LOG_WARN("RealDriver: Failed to send waypoints (sent {} bytes, expected {})", sent, totalSize);
        }
    }
}

void ControllerRealDriver::RegenerateWaypointsForLaneChange(int targetLaneId, double transitionDuration)
{
    waypoints_.clear();
    currentWaypointIndex_ = 0;

    if (!object_) return;

    double speed = std::max(object_->GetSpeed(), 5.0); // Minimum 5 m/s for calculation
    double transitionDist = speed * transitionDuration;
    double step = 5.0;        // 5m intervals
    double totalDist = 500.0; // Generate 500m ahead

    // [GT_MOD] Use real_vehicle_ position as base, NOT object_->pos_.
    // object_->pos_ may already be overwritten by LaneChangeAction at this point.
    roadmanager::Position posBase;
    posBase.SetInertiaPosMode(real_vehicle_.posX_, real_vehicle_.posY_, real_vehicle_.heading_,
                              roadmanager::Position::PosMode::H_ABS);
    int currentLaneId = posBase.GetLaneId();

    for (double d = 0; d < totalDist; d += step)
    {
        double progress = (transitionDist > 0) ? std::min(d / transitionDist, 1.0) : 1.0;
        double factor = 0.5 * (1.0 - std::cos(M_PI * progress)); // sinusoidal interpolation

        // Compute position in current lane and target lane at same s value
        roadmanager::Position posCur, posTgt;
        posCur.SetLanePos(posBase.GetTrackId(), currentLaneId, posBase.GetS(), 0);
        posTgt.SetLanePos(posBase.GetTrackId(), targetLaneId,  posBase.GetS(), 0);

        WaypointData wp;
        wp.x = posCur.GetX() * (1.0 - factor) + posTgt.GetX() * factor;
        wp.y = posCur.GetY() * (1.0 - factor) + posTgt.GetY() * factor;
        // [GT_MOD] Interpolate heading with angle wrapping (not just target heading)
        double hCur = posCur.GetH();
        double hTgt = posTgt.GetH();
        double hDiff = hTgt - hCur;
        while (hDiff > M_PI)  hDiff -= 2.0 * M_PI;
        while (hDiff < -M_PI) hDiff += 2.0 * M_PI;
        wp.h = hCur + factor * hDiff;
        wp.roadId = static_cast<uint32_t>(posBase.GetTrackId());
        wp.s = posBase.GetS();
        wp.laneId = targetLaneId;
        wp.laneOffset = 0;
        waypoints_.push_back(wp);

        // Advance to next s position along road
        roadmanager::Position::ReturnCode rc = posBase.MoveAlongS(step);
        if (static_cast<int>(rc) < 0) break;
    }

    LOG_INFO("RealDriver: Regenerated {} waypoints for lane change (target lane {})",
             waypoints_.size(), targetLaneId);

    // [DIAG] 最初の5個のウェイポイントを表示
    for (size_t i = 0; i < std::min(waypoints_.size(), size_t(5)); ++i) {
        LOG_INFO("[DIAG] WP_GEN[{}] x={:.2f} y={:.2f} h={:.4f} s={:.1f} lane={}",
                 i, waypoints_[i].x, waypoints_[i].y, waypoints_[i].h,
                 waypoints_[i].s, waypoints_[i].laneId);
    }
    LOG_INFO("[DIAG] WP_GEN base: posBase roadId={} laneId={} s={:.2f}",
             posBase.GetTrackId(), currentLaneId, posBase.GetS());
}

} // namespace gt_esmini
