#include "gt_esmini/control/ControllerRealDriver.hpp"
#include "gt_esmini/control/ControllerRealDriverUtils.hpp"
#include "gt_esmini/control/DriverInputReceiver.hpp"
#include "gt_esmini/control/VehicleStateUpdater.hpp"
#include "gt_esmini/control/EsminiStateApplier.hpp"
#include "gt_esmini/control/ControlDecisionEngine.hpp"
#include "gt_esmini/core/ConfigLoader.hpp"
#include <windows.h> // For GetModuleFileName
#include <cmath>     // For std::sqrt, std::atan2, M_PI
#include <algorithm>
#include "logger.hpp"
#include "ScenarioGateway.hpp"
#include "Entities.hpp"
#include "gt_esmini/scenario/ExtraEntities.hpp" // For Light Extension
#include "gt_esmini/control/TerrainTracker.hpp" // For terrain tracking
#include "gt_esmini/osi/GT_HostVehicleReporter.hpp"
#include "Storyboard.hpp"      // For Event
#include "OSCPrivateAction.hpp" // For LongSpeedAction
#include "Action.hpp"          // For OSCAction::ActionType

namespace gt_esmini
{
namespace
{
WaypointData MakeWaypointFromPosition(const roadmanager::Position& pos, double laneOffsetOverride)
{
    WaypointData wp;
    wp.x = pos.GetX();
    wp.y = pos.GetY();
    wp.h = pos.GetH();
    wp.roadId = static_cast<uint32_t>(pos.GetTrackId());
    wp.s = pos.GetS();
    wp.laneId = pos.GetLaneId();
    wp.laneOffset = laneOffsetOverride;
    return wp;
}

double ResolveLaneOffsetTarget(const scenarioengine::LatLaneOffsetAction& action, double currentOffset)
{
    double targetOffset = currentOffset;
    if (!action.target_)
    {
        return targetOffset;
    }

    if (action.target_->type_ == scenarioengine::LatLaneOffsetAction::Target::Type::ABSOLUTE_OFFSET)
    {
        return action.target_->value_;
    }

    auto* targetRel = static_cast<scenarioengine::LatLaneOffsetAction::TargetRelative*>(action.target_.get());
    double refOffset = currentOffset;
    if (targetRel && targetRel->object_)
    {
        refOffset = targetRel->object_->pos_.GetOffset();
    }
    return refOffset + action.target_->value_;
}
}  // namespace

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
      waypointsExtracted_(false),
      driver_input_receiver_(new DriverInputReceiver()),
      vehicle_state_updater_(new VehicleStateUpdater()),
      esmini_state_applier_(new EsminiStateApplier()),
      control_decision_engine_(new ControlDecisionEngine())
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
    delete driver_input_receiver_;
    delete vehicle_state_updater_;
    delete esmini_state_applier_;
    delete control_decision_engine_;
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
        ConfigLoader config_loader;
        std::string paramFile = config_loader.ResolveConfigPath(exeDir, "real_vehicle_params.json");

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

ControllerRealDriver::RunningActionState ControllerRealDriver::GetRunningActionState()
{
    RunningActionState state;
    GetTargetSpeedFromActions(&state.hasRunningScenarioLongAction);

    state.longDistance = GetRunningLongDistanceAction();
    state.speedProfile = GetRunningSpeedProfileAction();
    state.synchronize = GetRunningSynchronizeAction();
    state.hasRunningScenarioLongAction =
        state.hasRunningScenarioLongAction ||
        (state.longDistance != nullptr) ||
        (state.speedProfile != nullptr) ||
        (state.synchronize != nullptr);

    state.laneChange = GetRunningLaneChangeAction();
    state.laneOffset = GetRunningLaneOffsetAction();
    state.followTrajectory = GetRunningFollowTrajectoryAction();
    state.assignRoute = GetRunningAssignRouteAction();
    return state;
}

ControllerRealDriver::ActionFlags ControllerRealDriver::ToActionFlags(const RunningActionState& state)
{
    ActionFlags flags;
    flags.laneChanging = (state.laneChange != nullptr);
    flags.laneOffsetting = (state.laneOffset != nullptr);
    flags.followingTrajectory = (state.followTrajectory != nullptr);
    flags.assigningRoute = (state.assignRoute != nullptr);
    return flags;
}

void ControllerRealDriver::UpdateSetSpeedFromScenarioObject()
{
    if (!object_)
    {
        return;
    }

    const double objectSpeed = object_->GetSpeed();
    if (std::abs(objectSpeed - currentSpeed_) > 1e-3)
    {
        LOG_INFO("RealDriver: Detected speed change from scenario: {:.2f} -> {:.2f} m/s",
                 currentSpeed_, objectSpeed);
        setSpeed_ = objectSpeed;
        currentSpeed_ = objectSpeed;
    }
}

bool ControllerRealDriver::ParseDriverInputPacket(int packetSize)
{
    if (packetSize < 4)
    {
        LOG_WARN("RealDriverController: Packet too small ({})", packetSize);
        std::cerr << "RealDriverController: Packet too small (" << packetSize << " bytes)" << std::endl;
        return false;
    }

    int* maskPtr = reinterpret_cast<int*>(udp_buffer_.data());
    input_.lightMask = *maskPtr;
    if (!cached_hvd_.ParseFromArray(udp_buffer_.data() + 4, packetSize - 4))
    {
        LOG_ERROR("RealDriverController: Failed to parse HostVehicleData");
        std::cerr << "RealDriverController: Failed to parse HostVehicleData (" << (packetSize - 4) << " bytes)" << std::endl;
        return false;
    }

    static int log_counter = 0;
    if (log_counter++ % 50 == 0)
    {
        std::cout << "RealDriverController: Packet Received (" << packetSize
                  << " bytes). LightMask=" << input_.lightMask << std::endl;
    }

    if (cached_hvd_.has_vehicle_powertrain())
    {
        input_.throttle = cached_hvd_.vehicle_powertrain().pedal_position_acceleration();
        input_.gear = cached_hvd_.vehicle_powertrain().gear_transmission();
    }
    else
    {
        input_.throttle = 0.0;
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

    input_.engineBrake = 0.49;
    input_.adasStates.assign(realdetail::kAdasFunctionCount, 0);
    for (const auto& func : cached_hvd_.vehicle_automated_driving_function())
    {
        if (func.custom_name().empty())
        {
            continue;
        }
        const int idx = realdetail::MapAdasFunctionNameToIndex(func.custom_name());
        if (idx >= 0 && idx < static_cast<int>(realdetail::kAdasFunctionCount))
        {
            input_.adasStates[idx] = static_cast<int>(func.state());
        }
    }

    return true;
}

void ControllerRealDriver::ReceiveLatestUdpInput()
{
    if (!udpServer_)
    {
        return;
    }

    while (true)
    {
        const int received = udpServer_->Receive(udp_buffer_.data(), static_cast<int>(udp_buffer_.size()));
        if (received <= 0)
        {
            break;
        }
        ParseDriverInputPacket(received);
    }
}

void ControllerRealDriver::UpdateVehiclePhysics(double timeStep)
{
    real_vehicle_.SetEngineBrakeFactor(input_.engineBrake);

    double terrain_pitch = 0.0;
    double terrain_roll = 0.0;
    if (object_ && TerrainTracker::IsEnabled())
    {
        terrain_pitch = object_->pos_.GetP();
        terrain_roll = object_->pos_.GetR();
    }
    real_vehicle_.SetTerrainAttitude(terrain_pitch, terrain_roll);

    static double last_steering_debug = 0.0;
    const double steering_rate = (input_.steering - last_steering_debug) / (timeStep > 0 ? timeStep : 0.01);
    if (std::abs(steering_rate) > 8.0 && wasLaneChanging_)
    {
        LOG_WARN("RealDriver: [DEBUG] High Steering Rate detected: {:.2f}/s (Last={:.3f}, Curr={:.3f})",
                 steering_rate, last_steering_debug, input_.steering);
    }
    last_steering_debug = input_.steering;

    real_vehicle_.UpdatePhysics(timeStep, input_.throttle, input_.brake, input_.steering, input_.gear);
    currentSpeed_ = real_vehicle_.speed_;
}

void ControllerRealDriver::SendTargetSpeedPacket()
{
    if (!udpClient_)
    {
        return;
    }

#pragma pack(push, 1)
    struct TargetSpeedPacket {
        uint8_t type;
        double targetSpeed;
    } packet;
#pragma pack(pop)

    packet.type = 1;
    packet.targetSpeed = setSpeed_;
    const int sent = udpClient_->Send(reinterpret_cast<char*>(&packet), sizeof(packet));
    if (sent != sizeof(packet))
    {
        static int error_counter = 0;
        if (error_counter++ % 100 == 0)
        {
            LOG_WARN("RealDriver: Failed to send target speed (sent {} bytes, expected {})", sent, sizeof(packet));
        }
    }
}

void ControllerRealDriver::MaybeSendWaypoints()
{
    if (!sendWaypoints_)
    {
        return;
    }
    if (!waypointsExtracted_)
    {
        ExtractWaypoints();
        waypointsExtracted_ = true;
    }
    SendWaypointsUDP();
}

void ControllerRealDriver::UpdateCachedPowertrain()
{
    auto* powertrain = cached_hvd_.mutable_vehicle_powertrain();
    if (powertrain->motor_size() == 0)
    {
        powertrain->add_motor();
    }

    auto* motor = powertrain->mutable_motor(0);
    motor->set_rpm(real_vehicle_.GetRPM());
    motor->set_torque(real_vehicle_.GetTorqueOutput());
}

void ControllerRealDriver::UpdateHostVehicleReporter() const
{
    if (object_)
    {
        GT_HostVehicleReporter::Instance().SetBaseHostVehicleData(object_->GetId(), cached_hvd_);
    }
}

bool ControllerRealDriver::HandlePathActions(
    const RunningActionState& state, const ActionFlags& previousFlags, const char* phaseLabel)
{
    bool pathActionStarted = false;

    if (!previousFlags.followingTrajectory && state.followTrajectory)
    {
        LOG_INFO("RealDriver: {}FollowTrajectory detected, converting trajectory to waypoints", phaseLabel);
        RegenerateWaypointsForTrajectory(state.followTrajectory);
        state.followTrajectory->End();
        LOG_INFO("RealDriver: {}FollowTrajectory action force-completed", phaseLabel);
        pathActionStarted = true;
    }

    if (!pathActionStarted && !previousFlags.laneChanging && state.laneChange && state.laneChange->target_)
    {
        const int targetLaneId = state.laneChange->target_->value_;
        const double duration = state.laneChange->transition_.GetParamTargetVal();
        LOG_INFO("RealDriver: {}LaneChange detected, target lane={}, duration={:.1f}s",
                 phaseLabel, targetLaneId, duration);
        RegenerateWaypointsForLaneChange(targetLaneId, duration);
        state.laneChange->End();
        LOG_INFO("RealDriver: {}LaneChange action force-completed", phaseLabel);
        pathActionStarted = true;
    }

    if (!pathActionStarted && !previousFlags.laneOffsetting && state.laneOffset)
    {
        const double currentOffset = object_ ? object_->pos_.GetOffset() : 0.0;
        const double targetOffset = ResolveLaneOffsetTarget(*state.laneOffset, currentOffset);
        const double paramValue = state.laneOffset->transition_.GetParamTargetVal();
        const double speedForTime = object_ ? std::max(object_->GetSpeed(), 5.0) : 5.0;
        const double deltaOffset = std::abs(targetOffset - currentOffset);
        const double transitionDistance = realdetail::ComputeLaneOffsetTransitionDistance(
            state.laneOffset->transition_.dimension_, paramValue, speedForTime, deltaOffset);

        LOG_INFO("RealDriver: {}LaneOffset detected, target offset={:.2f}m, transition distance={:.1f}m",
                 phaseLabel, targetOffset, transitionDistance);
        RegenerateWaypointsForLaneOffset(targetOffset, transitionDistance);
        state.laneOffset->End();
        LOG_INFO("RealDriver: {}LaneOffset action force-completed", phaseLabel);
        pathActionStarted = true;
    }

    if (!pathActionStarted && !previousFlags.assigningRoute && state.assignRoute)
    {
        LOG_INFO("RealDriver: {}AssignRoute detected, refreshing route waypoints", phaseLabel);
        ExtractWaypoints();
        waypointsExtracted_ = true;
        state.assignRoute->End();
        LOG_INFO("RealDriver: {}AssignRoute action force-completed", phaseLabel);
        pathActionStarted = true;
    }

    return pathActionStarted;
}

void ControllerRealDriver::SyncObjectPoseFromRealVehicle()
{
    if (!object_)
    {
        return;
    }

    double dx, dy, dz_unused;
    real_vehicle_.GetBodyPositionOffset(dx, dy, dz_unused);
    const double h = real_vehicle_.heading_;
    const double w_dx = dx * std::cos(h) - dy * std::sin(h);
    const double w_dy = dx * std::sin(h) + dy * std::cos(h);
    object_->pos_.SetInertiaPos(real_vehicle_.posX_ + w_dx, real_vehicle_.posY_ + w_dy, real_vehicle_.heading_);
    object_->SetDirtyBits(scenarioengine::Object::DirtyBit::LATERAL | scenarioengine::Object::DirtyBit::LONGITUDINAL);
}

void ControllerRealDriver::SyncGatewayObjectState(double combinedPitch, double combinedRoll, bool blockSpeedUpdate)
{
    if (!object_ || !gateway_)
    {
        return;
    }

    double dx, dy, dz;
    real_vehicle_.GetBodyPositionOffset(dx, dy, dz);
    const double h = real_vehicle_.heading_;
    const double w_dx = dx * std::cos(h) - dy * std::sin(h);
    const double w_dy = dx * std::sin(h) + dy * std::cos(h);

    gateway_->updateObjectWorldPosXYH(
        object_->id_, 0.0, real_vehicle_.posX_ + w_dx, real_vehicle_.posY_ + w_dy, real_vehicle_.heading_);
    if (!blockSpeedUpdate)
    {
        gateway_->updateObjectSpeed(object_->id_, 0.0, real_vehicle_.speed_);
    }
    gateway_->updateObjectWheelAngle(object_->id_, 0.0, real_vehicle_.wheelAngle_);
    gateway_->updateObjectWorldPos(
        object_->id_,
        0.0,
        real_vehicle_.posX_ + w_dx,
        real_vehicle_.posY_ + w_dy,
        real_vehicle_.posZ_ + dz,
        real_vehicle_.heading_,
        combinedPitch,
        combinedRoll);

    SyncObjectPoseFromRealVehicle();
}

void ControllerRealDriver::UpdateVehicleLights()
{
    if (!object_)
    {
        return;
    }

    auto* vehicle = dynamic_cast<scenarioengine::Vehicle*>(object_);
    if (!vehicle)
    {
        return;
    }

    auto* ext = VehicleExtensionManager::Instance().GetExtension(vehicle);
    if (!ext)
    {
        return;
    }

    auto set_light = [&](VehicleLightType type, bool on) {
        LightState s;
        s.mode = on ? LightState::Mode::ON : LightState::Mode::OFF;
        ext->SetLightState(type, s);
    };

    const int mask = input_.lightMask;
    set_light(VehicleLightType::LOW_BEAM, (mask & 1));
    set_light(VehicleLightType::HIGH_BEAM, (mask & 2));
    set_light(VehicleLightType::INDICATOR_LEFT, (mask & 4));
    set_light(VehicleLightType::INDICATOR_RIGHT, (mask & 8));
    set_light(VehicleLightType::FOG_LIGHTS, (mask & 16));
    set_light(VehicleLightType::BRAKE_LIGHTS, (input_.brake > 0.05));
    set_light(VehicleLightType::REVERSING_LIGHTS, (input_.gear == -1));
}

void ControllerRealDriver::RefreshWaypointsOnRoutePointerChange()
{
    if (!object_)
    {
        return;
    }

    const roadmanager::Route* currentRoute = object_->pos_.GetRoute();
    if (currentRoute != nullptr && currentRoute != lastObservedRoute_)
    {
        LOG_INFO("RealDriver: Detected route change, refreshing route waypoints");
        ExtractWaypoints();
        waypointsExtracted_ = true;
    }
    lastObservedRoute_ = currentRoute;
}

void ControllerRealDriver::Step(double timeStep)
{
    const RunningActionState preStepState = GetRunningActionState();
    const ActionFlags previousFlags{wasLaneChanging_, wasLaneOffsetting_, wasFollowingTrajectory_, wasAssigningRoute_};

    control_decision_engine_->UpdateSetSpeed(*this);
    driver_input_receiver_->Receive(*this);
    vehicle_state_updater_->UpdatePhysics(*this, timeStep);
    SendTargetSpeedPacket();
    MaybeSendWaypoints();
    UpdateCachedPowertrain();
    UpdateHostVehicleReporter();

    double combined_pitch = 0.0;
    double combined_roll = 0.0;
    real_vehicle_.GetCombinedAttitude(combined_pitch, combined_roll);

    if (object_ && gateway_)
    {
        HandlePathActions(preStepState, previousFlags, "");
        esmini_state_applier_->Apply(*this, combined_pitch, combined_roll, preStepState.hasRunningScenarioLongAction);
        UpdateVehicleLights();
    }

    const ActionFlags preFlags = ToActionFlags(preStepState);
    wasLaneChanging_ = preFlags.laneChanging;
    wasLaneOffsetting_ = preFlags.laneOffsetting;
    wasFollowingTrajectory_ = preFlags.followingTrajectory;
    wasAssigningRoute_ = preFlags.assigningRoute;

    Controller::Step(timeStep);

    RefreshWaypointsOnRoutePointerChange();

    const RunningActionState postStepState = GetRunningActionState();
    const ActionFlags preControllerStepFlags{wasLaneChanging_, wasLaneOffsetting_, wasFollowingTrajectory_, wasAssigningRoute_};
    const bool postPathActionStarted = HandlePathActions(postStepState, preControllerStepFlags, "Post-step ");
    if (postPathActionStarted && object_)
    {
        SyncObjectPoseFromRealVehicle();
    }

    const ActionFlags postFlags = ToActionFlags(postStepState);
    wasLaneChanging_ = postFlags.laneChanging;
    wasLaneOffsetting_ = postFlags.laneOffsetting;
    wasFollowingTrajectory_ = postFlags.followingTrajectory;
    wasAssigningRoute_ = postFlags.assigningRoute;
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
        const double step = realdetail::kWaypointStep;                 // 5m intervals
        const double total_dist = realdetail::kWaypointTotalDistance;  // Generate for 500m ahead

        for (double d = 0; d < total_dist; d += step)
        {
            waypoints_.push_back(MakeWaypointFromPosition(pos, pos.GetOffset()));

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
        waypoints_.push_back(MakeWaypointFromPosition(wp, wp.GetOffset()));  // lane offset from lane center
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
            const double dist = realdetail::Distance2D(vehicleX, vehicleY, waypoints_[i].x, waypoints_[i].y);

            // Check if waypoint is close enough to consider
            if (dist < realdetail::kNearbyWaypointThreshold)  // Within 10m
            {
                // Check if waypoint is behind us by comparing heading
                const double headingToWp = std::atan2(dy, dx);
                const double angleDiff = realdetail::NormalizeAngle(headingToWp - vehicleH);

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
    const double step = realdetail::kWaypointStep;
    const double totalDist = realdetail::kWaypointTotalDistance;
    const double distForTransition = std::max(transitionDistance, 1.0);

    for (double d = 0.0; d < totalDist; d += step)
    {
        const double progress = std::min(d / distForTransition, 1.0);
        const double factor = realdetail::SmootherStep(progress);
        const double laneOffset = startOffset + (targetOffset - startOffset) * factor;

        roadmanager::Position offsetPos;
        offsetPos.SetLanePos(posBase.GetTrackId(), posBase.GetLaneId(), posBase.GetS(), laneOffset);

        WaypointData wp = MakeWaypointFromPosition(offsetPos, laneOffset);
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

    const double step = realdetail::kWaypointStep;
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
    const double step = realdetail::kWaypointStep;                 // 5m intervals
    const double totalDist = realdetail::kWaypointTotalDistance;   // Generate 500m ahead

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
        double factor = realdetail::SmootherStep(progress);

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
        double hDiff = realdetail::NormalizeAngle(hTgt - hCur);
        wp.h = hCur + factor * hDiff;
        wp.roadId = static_cast<uint32_t>(posBase.GetTrackId());
        wp.s = posBase.GetS();
        wp.laneId = targetLaneId;

        // [GT_MOD] Calculate laneOffset relative to targetLaneId.
        // This is crucial for the Python router to know we are not AT the lane center yet.
        // Logic: Higher LaneID is to the Left (e.g. +2 > +1 > -1 > -2).
        // If Target > Start (Left move), Start is to the Right -> Negative Offset.
        double lateralDist = realdetail::Distance2D(posCur.GetX(), posCur.GetY(), posTgt.GetX(), posTgt.GetY());
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
        double dh = realdetail::NormalizeAngle(waypoints_[i+1].h - waypoints_[i].h);
        double dist = realdetail::Distance2D(waypoints_[i].x, waypoints_[i].y, waypoints_[i+1].x, waypoints_[i+1].y);

        // Warn if heading change is > 5 degrees (0.087 rad) over a short distance
        if (dist > 0.1 && std::abs(dh) > realdetail::kSharpTurnWarnRad) { 
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
