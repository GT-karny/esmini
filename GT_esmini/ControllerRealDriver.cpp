#include "ControllerRealDriver.hpp"
#include <windows.h> // For GetModuleFileName
#include <cmath>     // For std::sqrt, std::atan2, M_PI
#include <algorithm>
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
        lastObservedRoute_ = object_->pos_.GetRoute();
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

    auto* speedAction = static_cast<scenarioengine::LongSpeedAction*>(
        GetRunningPrivateActionByType(scenarioengine::OSCAction::ActionType::LONG_SPEED));
    if (speedAction && speedAction->target_)
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

    // Natural-driving longitudinal actions that should block controller speed overwrite.
    if (GetRunningPrivateActionByType(scenarioengine::OSCAction::ActionType::LONG_SPEED_PROFILE) ||
        GetRunningPrivateActionByType(scenarioengine::OSCAction::ActionType::LONG_DISTANCE) ||
        GetRunningPrivateActionByType(scenarioengine::OSCAction::ActionType::SYNCHRONIZE_ACTION))
    {
        found       = true;
        targetSpeed = object_->GetSpeed();
    }

    if (hasRunningAction) *hasRunningAction = found;
    return targetSpeed;
}

scenarioengine::OSCPrivateAction* ControllerRealDriver::GetRunningPrivateActionByType(scenarioengine::OSCAction::ActionType type)
{
    if (!object_) return nullptr;

    // 1. Search initActions_
    for (auto* action : object_->initActions_)
    {
        if (action->action_type_ == type &&
            action->GetCurrentState() == scenarioengine::StoryBoardElement::State::RUNNING)
        {
            return action;
        }
    }

    // 2. Search objectEvents_
    for (auto* event : object_->objectEvents_)
    {
        for (auto* action : event->action_)
        {
            if (action->GetBaseType() == scenarioengine::OSCAction::BaseType::PRIVATE)
            {
                auto* pa = static_cast<scenarioengine::OSCPrivateAction*>(action);
                if (pa->action_type_ == type &&
                    pa->GetCurrentState() == scenarioengine::StoryBoardElement::State::RUNNING)
                {
                    return pa;
                }
            }
        }
    }
    return nullptr;
}

scenarioengine::LatLaneChangeAction* ControllerRealDriver::GetRunningLaneChangeAction()
{
    return static_cast<scenarioengine::LatLaneChangeAction*>(
        GetRunningPrivateActionByType(scenarioengine::OSCAction::ActionType::LAT_LANE_CHANGE));
}

scenarioengine::LatLaneOffsetAction* ControllerRealDriver::GetRunningLaneOffsetAction()
{
    return static_cast<scenarioengine::LatLaneOffsetAction*>(
        GetRunningPrivateActionByType(scenarioengine::OSCAction::ActionType::LAT_LANE_OFFSET));
}

scenarioengine::LongDistanceAction* ControllerRealDriver::GetRunningLongDistanceAction()
{
    return static_cast<scenarioengine::LongDistanceAction*>(
        GetRunningPrivateActionByType(scenarioengine::OSCAction::ActionType::LONG_DISTANCE));
}

scenarioengine::LongSpeedProfileAction* ControllerRealDriver::GetRunningSpeedProfileAction()
{
    return static_cast<scenarioengine::LongSpeedProfileAction*>(
        GetRunningPrivateActionByType(scenarioengine::OSCAction::ActionType::LONG_SPEED_PROFILE));
}

scenarioengine::FollowTrajectoryAction* ControllerRealDriver::GetRunningFollowTrajectoryAction()
{
    return static_cast<scenarioengine::FollowTrajectoryAction*>(
        GetRunningPrivateActionByType(scenarioengine::OSCAction::ActionType::FOLLOW_TRAJECTORY));
}

scenarioengine::SynchronizeAction* ControllerRealDriver::GetRunningSynchronizeAction()
{
    return static_cast<scenarioengine::SynchronizeAction*>(
        GetRunningPrivateActionByType(scenarioengine::OSCAction::ActionType::SYNCHRONIZE_ACTION));
}

scenarioengine::AssignRouteAction* ControllerRealDriver::GetRunningAssignRouteAction()
{
    return static_cast<scenarioengine::AssignRouteAction*>(
        GetRunningPrivateActionByType(scenarioengine::OSCAction::ActionType::ASSIGN_ROUTE));
}

void ControllerRealDriver::Step(double timeStep)
{
    // Note: TerrainTracker::UpdateAllVehicleTerrain() is now called from GT_Step()
    // to avoid dependency issues with ScenarioEngine access

    // 0. Detect scenario actions. Running longitudinal actions must block gateway speed overwrite,
    // otherwise scenario dynamics are suppressed by controller-written speed.
    bool hasRunningScenarioLongAction = false;
    GetTargetSpeedFromActions(&hasRunningScenarioLongAction);
    auto* runningLongDistanceAction = GetRunningLongDistanceAction();
    auto* runningSpeedProfileAction = GetRunningSpeedProfileAction();
    auto* runningSynchronizeAction = GetRunningSynchronizeAction();
    hasRunningScenarioLongAction =
        hasRunningScenarioLongAction ||
        (runningLongDistanceAction != nullptr) ||
        (runningSpeedProfileAction != nullptr) ||
        (runningSynchronizeAction != nullptr);

    // [GT_MOD] Check for running path/lateral actions to regenerate waypoints on start.
    auto* runningLaneChangeAction = GetRunningLaneChangeAction();
    bool hasRunningLaneChange = (runningLaneChangeAction != nullptr);
    auto* runningLaneOffsetAction = GetRunningLaneOffsetAction();
    bool hasRunningLaneOffset = (runningLaneOffsetAction != nullptr);
    auto* runningFollowTrajectoryAction = GetRunningFollowTrajectoryAction();
    bool hasRunningFollowTrajectory = (runningFollowTrajectoryAction != nullptr);
    auto* runningAssignRouteAction = GetRunningAssignRouteAction();
    bool hasRunningAssignRoute = (runningAssignRouteAction != nullptr);

    double objectSpeed = object_->GetSpeed();
    if (std::abs(objectSpeed - currentSpeed_) > 1e-3)
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

    // [DEBUG] Monitor Steering Rate
    static double last_steering_debug = 0.0;
    double steering_rate = (input_.steering - last_steering_debug) / (timeStep > 0 ? timeStep : 0.01);
    if (std::abs(steering_rate) > 8.0 && wasLaneChanging_) { // High rate check
        LOG_WARN("RealDriver: [DEBUG] High Steering Rate detected: {:.2f}/s (Last={:.3f}, Curr={:.3f})", 
                 steering_rate, last_steering_debug, input_.steering);
    }
    last_steering_debug = input_.steering;

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
        // Detect start of path-relevant actions and convert them to waypoint targets.
        // Priority: FollowTrajectory > LaneChange > LaneOffset > AssignRoute.
        bool pathActionStarted = false;
        if (!wasFollowingTrajectory_ && hasRunningFollowTrajectory && runningFollowTrajectoryAction)
        {
            LOG_INFO("RealDriver: FollowTrajectory starting, converting trajectory to waypoints");
            RegenerateWaypointsForTrajectory(runningFollowTrajectoryAction);
            runningFollowTrajectoryAction->End();
            LOG_INFO("RealDriver: FollowTrajectory action force-completed (waypoints provide target)");
            pathActionStarted = true;
        }
        if (!pathActionStarted && !wasLaneChanging_ && hasRunningLaneChange)
        {
            if (runningLaneChangeAction && runningLaneChangeAction->target_)
            {
                int targetLaneId = runningLaneChangeAction->target_->value_;
                double duration  = runningLaneChangeAction->transition_.GetParamTargetVal();
                LOG_INFO("RealDriver: LaneChange starting, target lane={}, duration={:.1f}s",
                         targetLaneId, duration);
                RegenerateWaypointsForLaneChange(targetLaneId, duration);

                // [GT_MOD] Force-complete the action to prevent it from writing to object_->pos_.
                // Python steering priority: real_vehicle_ drives position via waypoints,
                // the action's direct pos writes cause 3D viewer oscillation.
                runningLaneChangeAction->End();
                LOG_INFO("RealDriver: LaneChange action force-completed (waypoints provide target)");
                pathActionStarted = true;
            }
        }
        if (!pathActionStarted && !wasLaneOffsetting_ && hasRunningLaneOffset && runningLaneOffsetAction)
        {
            double currentOffset = object_->pos_.GetOffset();
            double targetOffset  = currentOffset;

            if (runningLaneOffsetAction->target_)
            {
                if (runningLaneOffsetAction->target_->type_ == scenarioengine::LatLaneOffsetAction::Target::Type::ABSOLUTE_OFFSET)
                {
                    targetOffset = runningLaneOffsetAction->target_->value_;
                }
                else
                {
                    auto* targetRel = static_cast<scenarioengine::LatLaneOffsetAction::TargetRelative*>(
                        runningLaneOffsetAction->target_.get());
                    double refOffset = currentOffset;
                    if (targetRel && targetRel->object_)
                    {
                        refOffset = targetRel->object_->pos_.GetOffset();
                    }
                    targetOffset = refOffset + runningLaneOffsetAction->target_->value_;
                }
            }

            double transitionDistance = 20.0;
            const double paramValue   = runningLaneOffsetAction->transition_.GetParamTargetVal();
            const double speedForTime = std::max(object_->GetSpeed(), 5.0);
            const double deltaOffset  = std::abs(targetOffset - currentOffset);

            switch (runningLaneOffsetAction->transition_.dimension_)
            {
                case scenarioengine::OSCPrivateAction::DynamicsDimension::DISTANCE:
                    transitionDistance = std::max(paramValue, 5.0);
                    break;
                case scenarioengine::OSCPrivateAction::DynamicsDimension::TIME:
                    transitionDistance = speedForTime * std::max(paramValue, 0.1);
                    break;
                case scenarioengine::OSCPrivateAction::DynamicsDimension::RATE:
                    transitionDistance = speedForTime * (deltaOffset / std::max(paramValue, 0.1));
                    break;
                default:
                    transitionDistance = 20.0;
                    break;
            }

            LOG_INFO("RealDriver: LaneOffset starting, target offset={:.2f}m, transition distance={:.1f}m",
                     targetOffset, transitionDistance);
            RegenerateWaypointsForLaneOffset(targetOffset, transitionDistance);
            runningLaneOffsetAction->End();
            LOG_INFO("RealDriver: LaneOffset action force-completed (waypoints provide target)");
            pathActionStarted = true;
        }
        if (!pathActionStarted && !wasAssigningRoute_ && hasRunningAssignRoute && runningAssignRouteAction)
        {
            LOG_INFO("RealDriver: AssignRoute starting, refreshing route waypoints");
            ExtractWaypoints();
            waypointsExtracted_ = true;
            runningAssignRouteAction->End();
            LOG_INFO("RealDriver: AssignRoute action force-completed (waypoints extracted)");
            pathActionStarted = true;
        }

        wasLaneChanging_ = hasRunningLaneChange;
        wasLaneOffsetting_ = hasRunningLaneOffset;
        wasFollowingTrajectory_ = hasRunningFollowTrajectory;
        wasAssigningRoute_ = hasRunningAssignRoute;

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

        // Update Speed
        // Skip speed overwrite while scenario longitudinal actions are active.
        if (!hasRunningScenarioLongAction)
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

        // [GT_MOD] Sync object_->pos_ with real_vehicle_ for viewer consistency.
        // The 3D viewer (OSG) reads object_->pos_ directly for rendering.
        // Without this, scenario actions (LaneChangeAction etc.) could leave
        // stale trajectory data in object_->pos_, causing visual oscillation.
        object_->pos_.SetInertiaPos(
            real_vehicle_.posX_ + w_dx,
            real_vehicle_.posY_ + w_dy,
            real_vehicle_.heading_);
        object_->SetDirtyBits(scenarioengine::Object::DirtyBit::LATERAL | scenarioengine::Object::DirtyBit::LONGITUDINAL);

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

    // AssignRouteAction can complete within one storyboard step.
    // Detect route pointer changes and refresh waypoints even if no RUNNING state is observed.
    if (object_)
    {
        const roadmanager::Route* currentRoute = object_->pos_.GetRoute();
        if (currentRoute != nullptr && currentRoute != lastObservedRoute_)
        {
            LOG_INFO("RealDriver: Detected route change, refreshing route waypoints");
            ExtractWaypoints();
            waypointsExtracted_ = true;
        }
        lastObservedRoute_ = currentRoute;
    }

    // Re-check action state after Storyboard step.
    // This avoids one-frame delay when actions transition to RUNNING inside Controller::Step().
    auto* postStepLaneChangeAction = GetRunningLaneChangeAction();
    bool hasPostStepLaneChange = (postStepLaneChangeAction != nullptr);
    auto* postStepLaneOffsetAction = GetRunningLaneOffsetAction();
    bool hasPostStepLaneOffset = (postStepLaneOffsetAction != nullptr);
    auto* postStepFollowTrajectoryAction = GetRunningFollowTrajectoryAction();
    bool hasPostStepFollowTrajectory = (postStepFollowTrajectoryAction != nullptr);
    auto* postStepAssignRouteAction = GetRunningAssignRouteAction();
    bool hasPostStepAssignRoute = (postStepAssignRouteAction != nullptr);

    bool postPathActionStarted = false;
    if (!wasFollowingTrajectory_ && hasPostStepFollowTrajectory && postStepFollowTrajectoryAction)
    {
        LOG_INFO("RealDriver: Post-step FollowTrajectory detected");
        RegenerateWaypointsForTrajectory(postStepFollowTrajectoryAction);
        postStepFollowTrajectoryAction->End();
        LOG_INFO("RealDriver: Post-step FollowTrajectory action force-completed");
        postPathActionStarted = true;
    }
    if (!postPathActionStarted && !wasLaneChanging_ && hasPostStepLaneChange)
    {
        if (postStepLaneChangeAction && postStepLaneChangeAction->target_)
        {
            int targetLaneId = postStepLaneChangeAction->target_->value_;
            double duration  = postStepLaneChangeAction->transition_.GetParamTargetVal();
            LOG_INFO("RealDriver: Post-step LaneChange detected, target lane={}, duration={:.1f}s",
                     targetLaneId, duration);
            RegenerateWaypointsForLaneChange(targetLaneId, duration);
            postStepLaneChangeAction->End();
            LOG_INFO("RealDriver: Post-step LaneChange action force-completed");
            postPathActionStarted = true;
        }
    }
    if (!postPathActionStarted && !wasLaneOffsetting_ && hasPostStepLaneOffset && postStepLaneOffsetAction)
    {
        double currentOffset = object_ ? object_->pos_.GetOffset() : 0.0;
        double targetOffset  = currentOffset;

        if (postStepLaneOffsetAction->target_)
        {
            if (postStepLaneOffsetAction->target_->type_ == scenarioengine::LatLaneOffsetAction::Target::Type::ABSOLUTE_OFFSET)
            {
                targetOffset = postStepLaneOffsetAction->target_->value_;
            }
            else
            {
                auto* targetRel = static_cast<scenarioengine::LatLaneOffsetAction::TargetRelative*>(
                    postStepLaneOffsetAction->target_.get());
                double refOffset = currentOffset;
                if (targetRel && targetRel->object_)
                {
                    refOffset = targetRel->object_->pos_.GetOffset();
                }
                targetOffset = refOffset + postStepLaneOffsetAction->target_->value_;
            }
        }

        double transitionDistance = 20.0;
        const double paramValue   = postStepLaneOffsetAction->transition_.GetParamTargetVal();
        const double speedForTime = std::max(object_->GetSpeed(), 5.0);
        const double deltaOffset  = std::abs(targetOffset - currentOffset);

        switch (postStepLaneOffsetAction->transition_.dimension_)
        {
            case scenarioengine::OSCPrivateAction::DynamicsDimension::DISTANCE:
                transitionDistance = std::max(paramValue, 5.0);
                break;
            case scenarioengine::OSCPrivateAction::DynamicsDimension::TIME:
                transitionDistance = speedForTime * std::max(paramValue, 0.1);
                break;
            case scenarioengine::OSCPrivateAction::DynamicsDimension::RATE:
                transitionDistance = speedForTime * (deltaOffset / std::max(paramValue, 0.1));
                break;
            default:
                transitionDistance = 20.0;
                break;
        }

        LOG_INFO("RealDriver: Post-step LaneOffset detected, target offset={:.2f}m, transition distance={:.1f}m",
                 targetOffset, transitionDistance);
        RegenerateWaypointsForLaneOffset(targetOffset, transitionDistance);
        postStepLaneOffsetAction->End();
        LOG_INFO("RealDriver: Post-step LaneOffset action force-completed");
        postPathActionStarted = true;
    }
    if (!postPathActionStarted && !wasAssigningRoute_ && hasPostStepAssignRoute && postStepAssignRouteAction)
    {
        LOG_INFO("RealDriver: Post-step AssignRoute detected, refreshing route waypoints");
        ExtractWaypoints();
        waypointsExtracted_ = true;
        postStepAssignRouteAction->End();
        LOG_INFO("RealDriver: Post-step AssignRoute action force-completed");
        postPathActionStarted = true;
    }

    if (postPathActionStarted && object_)
    {
        // Keep object pose synced in the same frame to avoid transient trajectory artifacts.
        double dx, dy, dz_unused;
        real_vehicle_.GetBodyPositionOffset(dx, dy, dz_unused);
        double h = real_vehicle_.heading_;
        double w_dx = dx * std::cos(h) - dy * std::sin(h);
        double w_dy = dx * std::sin(h) + dy * std::cos(h);
        object_->pos_.SetInertiaPos(
            real_vehicle_.posX_ + w_dx,
            real_vehicle_.posY_ + w_dy,
            real_vehicle_.heading_);
        object_->SetDirtyBits(scenarioengine::Object::DirtyBit::LATERAL | scenarioengine::Object::DirtyBit::LONGITUDINAL);
    }

    wasLaneChanging_ = hasPostStepLaneChange;
    wasLaneOffsetting_ = hasPostStepLaneOffset;
    wasFollowingTrajectory_ = hasPostStepFollowTrajectory;
    wasAssigningRoute_ = hasPostStepAssignRoute;
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
        lastObservedRoute_ = nullptr;
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
    lastObservedRoute_ = route;

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

void ControllerRealDriver::RegenerateWaypointsForLaneOffset(double targetOffset, double transitionDistance)
{
    waypoints_.clear();
    currentWaypointIndex_ = 0;

    if (!object_) return;

    roadmanager::Position posBase;
    posBase.SetInertiaPosMode(real_vehicle_.posX_, real_vehicle_.posY_, real_vehicle_.heading_,
                              roadmanager::Position::PosMode::H_ABS);
    const double startOffset = posBase.GetOffset();
    const double step = 5.0;
    const double totalDist = 500.0;
    const double distForTransition = std::max(transitionDistance, 1.0);

    for (double d = 0.0; d < totalDist; d += step)
    {
        const double progress = std::min(d / distForTransition, 1.0);
        const double factor =
            progress * progress * progress * (progress * (progress * 6.0 - 15.0) + 10.0);
        const double laneOffset = startOffset + (targetOffset - startOffset) * factor;

        roadmanager::Position offsetPos;
        offsetPos.SetLanePos(posBase.GetTrackId(), posBase.GetLaneId(), posBase.GetS(), laneOffset);

        WaypointData wp;
        wp.x = offsetPos.GetX();
        wp.y = offsetPos.GetY();
        wp.h = offsetPos.GetH();
        wp.roadId = static_cast<uint32_t>(offsetPos.GetTrackId());
        wp.s = offsetPos.GetS();
        wp.laneId = offsetPos.GetLaneId();
        wp.laneOffset = laneOffset;
        waypoints_.push_back(wp);

        roadmanager::Position::ReturnCode rc = posBase.MoveAlongS(step);
        if (static_cast<int>(rc) < 0)
        {
            break;
        }
    }

    LOG_INFO("RealDriver: Regenerated {} waypoints for lane offset transition (start={:.2f}m target={:.2f}m)",
             waypoints_.size(), startOffset, targetOffset);
}

void ControllerRealDriver::RegenerateWaypointsForTrajectory(scenarioengine::FollowTrajectoryAction* action)
{
    waypoints_.clear();
    currentWaypointIndex_ = 0;

    if (!action || !action->traj_ || !action->traj_->shape_)
    {
        LOG_WARN("RealDriver: FollowTrajectory action has no valid trajectory shape");
        return;
    }

    const double step = 5.0;
    const double length = std::max(action->traj_->GetLength(), step);

    for (double s = 0.0; s <= length; s += step)
    {
        roadmanager::TrajVertex tv;
        if (action->traj_->shape_->Evaluate(s, roadmanager::Shape::TrajectoryParamType::TRAJ_PARAM_TYPE_S, tv) != 0)
        {
            continue;
        }

        WaypointData wp;
        wp.x = tv.x;
        wp.y = tv.y;
        wp.h = tv.h_true;

        roadmanager::Position pos;
        pos.SetInertiaPosMode(tv.x, tv.y, tv.h_true, roadmanager::Position::PosMode::H_ABS);
        if (pos.GetTrackId() != ID_UNDEFINED)
        {
            wp.roadId = static_cast<uint32_t>(pos.GetTrackId());
            wp.s = pos.GetS();
            wp.laneId = pos.GetLaneId();
            wp.laneOffset = pos.GetOffset();
        }
        else
        {
            wp.roadId = 0;
            wp.s = s;
            wp.laneId = 0;
            wp.laneOffset = 0.0;
        }

        waypoints_.push_back(wp);
    }

    if (waypoints_.empty())
    {
        LOG_WARN("RealDriver: FollowTrajectory conversion yielded no waypoints, keeping existing route");
        ExtractWaypoints();
        return;
    }

    LOG_INFO("RealDriver: Regenerated {} waypoints from FollowTrajectory", waypoints_.size());
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

    LOG_INFO("RealDriver: [DEBUG] LaneChange Start - Vehicle LaneOffset={:.3f}, Speed={:.2f}, TgtLane={}", 
             posBase.GetOffset(), speed, targetLaneId);

    for (double d = 0; d < totalDist; d += step)
    {
        double progress = (transitionDist > 0) ? std::min(d / transitionDist, 1.0) : 1.0;
        // [GT_MOD] Use SmootherStep (Quintic Hermite) interpolation for even smoother steering.
        // Cubic (SmoothStep) has non-zero jerk at start/end. 
        // Quintic (t^3 * (6t^2 - 15t + 10)) ensures zero acceleration at endpoints (C2 continuous),
        // providing the smoothest natural motion for a lane change.
        double factor = progress * progress * progress * (progress * (progress * 6.0 - 15.0) + 10.0);

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

        // [GT_MOD] Calculate laneOffset relative to targetLaneId.
        // This is crucial for the Python router to know we are not AT the lane center yet.
        // Logic: Higher LaneID is to the Left (e.g. +2 > +1 > -1 > -2).
        // If Target > Start (Left move), Start is to the Right -> Negative Offset.
        double lateralDist = std::sqrt(std::pow(posCur.GetX() - posTgt.GetX(), 2) + 
                                       std::pow(posCur.GetY() - posTgt.GetY(), 2));
        double sign = (currentLaneId < targetLaneId) ? -1.0 : 1.0;
        wp.laneOffset = lateralDist * sign * (1.0 - factor);

        if (d == 0) {
            printf("[RealDriver] LaneChange Start: CurLane=%d TgtLane=%d LatDist=%.3f Sign=%.1f Offset=%.3f\n",
                   currentLaneId, targetLaneId, lateralDist, sign, wp.laneOffset);
        }
        waypoints_.push_back(wp);

        // Advance to next s position along road
        roadmanager::Position::ReturnCode rc = posBase.MoveAlongS(step);
        if (static_cast<int>(rc) < 0) break;

        // [DEBUG] Log first few interpolation points
        if (d < 25.0) {
             LOG_INFO("RealDriver: [DEBUG] WP Gen d={:.1f}, factor={:.3f}, x={:.2f}, y={:.2f}, h={:.3f}", 
                      d, factor, wp.x, wp.y, wp.h);
        }
    }

    // [DEBUG] Check for sharp turns in generated waypoints
    for (size_t i = 0; i < waypoints_.size() - 1; ++i) {
        double dh = waypoints_[i+1].h - waypoints_[i].h;
        // Normalize angle difference
        while (dh > M_PI) dh -= 2*M_PI;
        while (dh < -M_PI) dh += 2*M_PI;
        
        double dist = std::sqrt(std::pow(waypoints_[i+1].x - waypoints_[i].x, 2) + 
                                std::pow(waypoints_[i+1].y - waypoints_[i].y, 2));

        // Warn if heading change is > 5 degrees (0.087 rad) over a short distance
        if (dist > 0.1 && std::abs(dh) > 0.087) { 
             LOG_WARN("RealDriver: [DEBUG] Sharp turn at WP[{}] (d~{:.1f}): dh={:.3f} rad ({:.1f} deg), dist={:.2f}m", 
                      i, i * step, dh, dh * 180.0 / M_PI, dist);
        }
    }

    LOG_INFO("RealDriver: Regenerated {} waypoints for lane change (target lane {})",
             waypoints_.size(), targetLaneId);

    // Debug: first 5 waypoints for verification
    for (size_t i = 0; i < std::min(waypoints_.size(), size_t(5)); ++i) {
        LOG_INFO("[DEBUG] WP_GEN[{}] x={:.2f} y={:.2f} h={:.4f} s={:.1f} lane={}",
                 i, waypoints_[i].x, waypoints_[i].y, waypoints_[i].h,
                 waypoints_[i].s, waypoints_[i].laneId);
    }
    LOG_INFO("[DEBUG] WP_GEN base: posBase roadId={} laneId={} s={:.2f}",
             posBase.GetTrackId(), currentLaneId, posBase.GetS());
}

} // namespace gt_esmini
