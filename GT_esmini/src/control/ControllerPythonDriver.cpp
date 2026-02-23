#ifdef GT_ENABLE_EMBEDDED_PYTHON

#include "gt_esmini/control/ControllerPythonDriver.hpp"
#include "gt_esmini/control/ControllerRealDriverUtils.hpp"
#include "gt_esmini/control/TerrainTracker.hpp"
#include "gt_esmini/control/pythondriver/PythonDriverBridge.hpp"
#include "gt_esmini/control/pythondriver/PythonDriverCoordinator.hpp"
#include "gt_esmini/core/ConfigLoader.hpp"
#include "gt_esmini/osi/GT_HostVehicleReporter.hpp"
#include "gt_esmini/scenario/ExtraEntities.hpp"

#include "Entities.hpp"
#include "ScenarioGateway.hpp"
#include "esminiLib.hpp"
#include "logger.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <unordered_map>
#include <windows.h>

namespace gt_esmini
{
namespace
{
constexpr int kLightUnset = -1;
constexpr int kLightAuto = 0;
constexpr int kLightOff = 1;
constexpr int kLightOn = 2;

VehicleLightType SlotToVehicleLight(ControllerLightSlot slot)
{
    switch (slot)
    {
    case ControllerLightSlot::LOW_BEAM: return VehicleLightType::LOW_BEAM;
    case ControllerLightSlot::HIGH_BEAM: return VehicleLightType::HIGH_BEAM;
    case ControllerLightSlot::LEFT_INDICATOR: return VehicleLightType::INDICATOR_LEFT;
    case ControllerLightSlot::RIGHT_INDICATOR: return VehicleLightType::INDICATOR_RIGHT;
    case ControllerLightSlot::FOG: return VehicleLightType::FOG_LIGHTS;
    case ControllerLightSlot::BRAKE: return VehicleLightType::BRAKE_LIGHTS;
    case ControllerLightSlot::REVERSE: return VehicleLightType::REVERSING_LIGHTS;
    default: return VehicleLightType::LOW_BEAM;
    }
}

WaypointData MakeWaypointFromPosition(const roadmanager::Position& pos, double laneOffsetOverride)
{
    WaypointData wp;
    wp.x          = pos.GetX();
    wp.y          = pos.GetY();
    wp.h          = pos.GetH();
    wp.roadId     = static_cast<uint32_t>(pos.GetTrackId());
    wp.s          = pos.GetS();
    wp.laneId     = pos.GetLaneId();
    wp.laneOffset = laneOffsetOverride;
    return wp;
}

double DetermineAdaptiveStep(const roadmanager::Position& pos)
{
    roadmanager::Position p1 = pos;
    roadmanager::Position p2 = pos;
    if (static_cast<int>(p1.MoveAlongS(1.0)) < 0 || static_cast<int>(p2.MoveAlongS(2.0)) < 0)
    {
        return realdetail::kWaypointStep;
    }

    const double dh = std::abs(realdetail::NormalizeAngle(p2.GetH() - p1.GetH()));
    constexpr double kStep2Threshold = 3.0 * M_PI / 180.0;
    constexpr double kStep1Threshold = 8.0 * M_PI / 180.0;

    if (dh > kStep1Threshold)
    {
        return 1.0;
    }
    if (dh > kStep2Threshold)
    {
        return 2.0;
    }
    return realdetail::kWaypointStep;
}

double ResolveLaneOffsetTarget(const scenarioengine::LatLaneOffsetAction& action, double currentOffset)
{
    if (!action.target_)
    {
        return currentOffset;
    }

    if (action.target_->type_ == scenarioengine::LatLaneOffsetAction::Target::Type::ABSOLUTE_OFFSET)
    {
        auto* targetAbs = static_cast<scenarioengine::LatLaneOffsetAction::TargetAbsolute*>(action.target_.get());
        return targetAbs ? targetAbs->value_ : currentOffset;
    }

    auto* targetRel = static_cast<scenarioengine::LatLaneOffsetAction::TargetRelative*>(action.target_.get());
    if (!targetRel)
    {
        return currentOffset;
    }

    return currentOffset + targetRel->value_;
}
}  // namespace

// Reuse from ControllerRealDriver.cpp
extern std::string GetCurrentModuleDirectory();

scenarioengine::Controller* InstantiateControllerPythonDriver(void* args)
{
    auto* initArgs = static_cast<scenarioengine::Controller::InitArgs*>(args);
    return new ControllerPythonDriver(initArgs);
}

ControllerPythonDriver::ControllerPythonDriver(InitArgs* args)
    : scenarioengine::Controller(args)
{
    if (args && args->properties)
    {
        if (args->properties->ValueExists("PythonScript"))
        {
            python_script_path_ = args->properties->GetValueStr("PythonScript");
        }
        if (args->properties->ValueExists("PythonClass"))
        {
            python_class_name_ = args->properties->GetValueStr("PythonClass");
        }
        if (args->properties->ValueExists("PythonHome"))
        {
            python_home_ = args->properties->GetValueStr("PythonHome");
        }
        if (args->properties->ValueExists("PythonTrace"))
        {
            const std::string trace = args->properties->GetValueStr("PythonTrace");
            python_trace_enabled_ = (trace == "on" || trace == "ON" || trace == "1" || trace == "true" || trace == "TRUE");
        }
        if (args->properties->ValueExists("PythonTraceDir"))
        {
            python_trace_dir_ = args->properties->GetValueStr("PythonTraceDir");
        }
    }

    python_bridge_       = new PythonDriverBridge();
    python_coordinator_  = new PythonDriverCoordinator();
    lon_profile_planner_ = new LonProfilePlanner();

    LOG_INFO("PythonDriverController: Created (script='{}', class='{}')", python_script_path_, python_class_name_);
}

ControllerPythonDriver::~ControllerPythonDriver()
{
    if (python_bridge_)
    {
        python_bridge_->Shutdown();
        delete python_bridge_;
        python_bridge_ = nullptr;
    }
    delete python_coordinator_;
    python_coordinator_ = nullptr;
    delete lon_profile_planner_;
    lon_profile_planner_ = nullptr;
}

int ControllerPythonDriver::Activate(const ControlActivationMode (&mode)[static_cast<unsigned int>(ControlDomains::COUNT)])
{
    LOG_INFO("PythonDriverController::Activate() called");

    if (!object_)
    {
        FailAndStop("PythonDriverController: Activate failed, no object bound to controller");
        return scenarioengine::Controller::Activate(mode);
    }

    auto* vehicle = dynamic_cast<scenarioengine::Vehicle*>(object_);
    if (vehicle)
    {
        auto* ext = VehicleExtensionManager::Instance().GetExtension(vehicle);
        if (!ext)
        {
            ext = new VehicleLightExtension(vehicle);
            VehicleExtensionManager::Instance().RegisterExtension(vehicle, ext);
            LOG_INFO("PythonDriverController: Registered VehicleLightExtension for vehicle ID {}", vehicle->GetId());
        }
    }

    real_vehicle_.Reset();
    real_vehicle_.SetPos(object_->pos_.GetX(), object_->pos_.GetY(), object_->pos_.GetZ(), object_->pos_.GetH());
    real_vehicle_.SetSpeed(object_->GetSpeed());
    real_vehicle_.SetLength(object_->boundingbox_.dimensions_.length_);

    currentSpeed_            = object_->GetSpeed();
    setSpeed_                = object_->GetSpeed();
    lastWrittenGatewaySpeed_ = object_->GetSpeed();
    currentWaypointIndex_ = 0;
    waypointGenerationVersion_ = 0;
    lastObservedRoute_  = object_->pos_.GetRoute();
    wasLaneChanging_ = false;
    wasLaneOffsetting_ = false;
    wasFollowingTrajectory_ = false;
    wasAssigningRoute_ = false;
    wasLongitudinalDistance_ = false;
    wasSpeedProfile_ = false;
    wasSynchronize_ = false;
    frame_action_context_ = {};
    input_.adasStates.assign(realdetail::kAdasFunctionCount, 0);
    input_.lights.fill(kLightUnset);
    for (auto& state : light_runtime_)
    {
        state.manual_override = false;
        state.manual_on = false;
    }

    std::string exeDir = GetCurrentModuleDirectory();
    ConfigLoader config_loader;
    std::string paramFile = config_loader.ResolveConfigPath(exeDir, "real_vehicle_params.json");
    real_vehicle_.LoadParameters(paramFile);

    resolved_script_path_ = python_script_path_;
    if (!resolved_script_path_.empty())
    {
        namespace fs = std::filesystem;
        fs::path candidate = fs::path(resolved_script_path_);
        if (!candidate.is_absolute())
        {
            // Resolve relative script path with robust fallbacks.
            // 1) current working directory (where GT_Sim is launched)
            // 2) executable directory
            // 3) parent directories of executable directory (typical build tree)
            if (fs::exists(candidate))
            {
                candidate = fs::absolute(candidate);
            }
            else
            {
                fs::path exe_path = fs::path(exeDir);
                fs::path from_exe = exe_path / candidate;
                if (fs::exists(from_exe))
                {
                    candidate = from_exe;
                }
                else
                {
                    fs::path probe = exe_path;
                    bool found = false;
                    for (int i = 0; i < 6; ++i)
                    {
                        probe = probe.parent_path();
                        if (probe.empty())
                        {
                            break;
                        }
                        fs::path p = probe / candidate;
                        if (fs::exists(p))
                        {
                            candidate = p;
                            found = true;
                            break;
                        }
                    }
                    if (!found)
                    {
                        candidate = fs::absolute(candidate);
                    }
                }
            }
        }
        resolved_script_path_ = candidate.lexically_normal().string();
    }

    if (python_home_.empty())
    {
#ifdef GT_EMBEDDED_PYTHON_HOME
        python_home_ = GT_EMBEDDED_PYTHON_HOME;
#endif
    }

    const char* odr_file = SE_GetODRFilename();
    const std::string xodr_path = odr_file ? odr_file : "";
    const int ego_id = object_->GetId();
    const double nominal_dt = 0.01;

    if (!python_bridge_->Initialize(
            resolved_script_path_,
            python_class_name_,
            python_home_,
            python_trace_enabled_,
            python_trace_dir_,
            xodr_path,
            nominal_dt,
            ego_id))
    {
        FailAndStop("PythonDriverController: Python initialization failed (" + python_bridge_->GetLastError() + ")");
    }

    return scenarioengine::Controller::Activate(mode);
}

void ControllerPythonDriver::Step(double timeStep)
{
    if (fatal_error_)
    {
        return;
    }

    if (!python_bridge_ || !python_bridge_->IsInitialized())
    {
        FailAndStop("PythonDriverController: Python bridge is not initialized");
        return;
    }

    python_coordinator_->RunFrame(*this, timeStep);
}

void ControllerPythonDriver::GetInputsForOSI(double& throttle, double& brake, double& steering, int& gear, int& lightMask) const
{
    throttle  = input_.throttle;
    brake     = input_.brake;
    steering  = input_.steering;
    gear      = input_.gear;
    lightMask = BuildLightMaskFromExtension();
}

void ControllerPythonDriver::GetPowertrainForOSI(double& rpm, double& torque) const
{
    rpm    = real_vehicle_.GetRPM();
    torque = real_vehicle_.GetTorqueOutput();
}

void ControllerPythonDriver::GetADASStates(std::vector<int>& states) const
{
    states = input_.adasStates;
}

void ControllerPythonDriver::UpdateSetSpeedFromScenarioObject()
{
    if (!object_)
    {
        return;
    }

    // Detect external speed changes from scenario actions (SpeedAction, SpeedProfileAction, etc.)
    // by comparing the object's current speed with what this controller last wrote to the gateway.
    // If they differ, a scenario action must have changed the speed externally → update setSpeed_.
    // If they match, the speed came from our own gateway write → ignore to avoid feedback loop.
    const double objectSpeed = object_->GetSpeed();
    if (std::abs(objectSpeed - lastWrittenGatewaySpeed_) > 1e-3)
    {
        setSpeed_ = objectSpeed;
    }
}

void ControllerPythonDriver::DetectSpeedActionTarget()
{
    // When the controller is in MODE_OVERRIDE on the longitudinal domain,
    // esmini's LongSpeedAction::Start() immediately calls End(), so the
    // action never enters RUNNING state.  We scan completed SpeedActions
    // and read their target speed, updating setSpeed_ when a new one fires.
    if (!object_)
    {
        return;
    }

    for (auto* event : object_->objectEvents_)
    {
        if (!event)
        {
            continue;
        }
        for (auto* action : event->action_)
        {
            if (!action || action->GetBaseType() != scenarioengine::OSCAction::BaseType::PRIVATE)
            {
                continue;
            }
            auto* pa = static_cast<scenarioengine::OSCPrivateAction*>(action);
            if (pa->action_type_ != scenarioengine::OSCAction::ActionType::LONG_SPEED)
            {
                continue;
            }

            auto state = pa->GetCurrentState();
            if (state != scenarioengine::StoryBoardElement::State::RUNNING &&
                state != scenarioengine::StoryBoardElement::State::COMPLETE)
            {
                continue;
            }

            // Skip actions we've already processed.
            if (std::find(processedSpeedActions_.begin(), processedSpeedActions_.end(), pa) != processedSpeedActions_.end())
            {
                continue;
            }

            auto* speedAction = static_cast<scenarioengine::LongSpeedAction*>(pa);
            if (speedAction->target_)
            {
                const double targetSpeed = speedAction->target_->GetValue();
                LOG_INFO("PythonDriverController: SpeedAction intercepted (target={:.3f}, previous setSpeed={:.3f})",
                         targetSpeed, setSpeed_);
                setSpeed_ = targetSpeed;
                processedSpeedActions_.push_back(pa);
            }
        }
    }
}

scenarioengine::OSCPrivateAction* ControllerPythonDriver::GetRunningPrivateActionByType(scenarioengine::OSCAction::ActionType type)
{
    if (!object_)
    {
        return nullptr;
    }

    for (auto* action : object_->initActions_)
    {
        if (!action || action->GetBaseType() != scenarioengine::OSCAction::BaseType::PRIVATE)
        {
            continue;
        }
        auto* pa = static_cast<scenarioengine::OSCPrivateAction*>(action);
        if (pa->action_type_ == type && pa->GetCurrentState() == scenarioengine::StoryBoardElement::State::RUNNING)
        {
            return pa;
        }
    }

    for (auto* event : object_->objectEvents_)
    {
        if (!event)
        {
            continue;
        }

        for (auto* action : event->action_)
        {
            if (!action || action->GetBaseType() != scenarioengine::OSCAction::BaseType::PRIVATE)
            {
                continue;
            }
            auto* pa = static_cast<scenarioengine::OSCPrivateAction*>(action);
            if (pa->action_type_ == type && pa->GetCurrentState() == scenarioengine::StoryBoardElement::State::RUNNING)
            {
                return pa;
            }
        }
    }

    return nullptr;
}

scenarioengine::LatLaneChangeAction* ControllerPythonDriver::GetRunningLaneChangeAction()
{
    return static_cast<scenarioengine::LatLaneChangeAction*>(
        GetRunningPrivateActionByType(scenarioengine::OSCAction::ActionType::LAT_LANE_CHANGE));
}

scenarioengine::LatLaneOffsetAction* ControllerPythonDriver::GetRunningLaneOffsetAction()
{
    return static_cast<scenarioengine::LatLaneOffsetAction*>(
        GetRunningPrivateActionByType(scenarioengine::OSCAction::ActionType::LAT_LANE_OFFSET));
}

scenarioengine::LongDistanceAction* ControllerPythonDriver::GetRunningLongDistanceAction()
{
    return static_cast<scenarioengine::LongDistanceAction*>(
        GetRunningPrivateActionByType(scenarioengine::OSCAction::ActionType::LONG_DISTANCE));
}

scenarioengine::LongSpeedProfileAction* ControllerPythonDriver::GetRunningSpeedProfileAction()
{
    return static_cast<scenarioengine::LongSpeedProfileAction*>(
        GetRunningPrivateActionByType(scenarioengine::OSCAction::ActionType::LONG_SPEED_PROFILE));
}

scenarioengine::FollowTrajectoryAction* ControllerPythonDriver::GetRunningFollowTrajectoryAction()
{
    return static_cast<scenarioengine::FollowTrajectoryAction*>(
        GetRunningPrivateActionByType(scenarioengine::OSCAction::ActionType::FOLLOW_TRAJECTORY));
}

scenarioengine::SynchronizeAction* ControllerPythonDriver::GetRunningSynchronizeAction()
{
    return static_cast<scenarioengine::SynchronizeAction*>(
        GetRunningPrivateActionByType(scenarioengine::OSCAction::ActionType::SYNCHRONIZE_ACTION));
}

scenarioengine::AssignRouteAction* ControllerPythonDriver::GetRunningAssignRouteAction()
{
    return static_cast<scenarioengine::AssignRouteAction*>(
        GetRunningPrivateActionByType(scenarioengine::OSCAction::ActionType::ASSIGN_ROUTE));
}

ControllerPythonDriver::RunningActionState ControllerPythonDriver::GetRunningActionState()
{
    RunningActionState state;
    state.laneChange = GetRunningLaneChangeAction();
    state.laneOffset = GetRunningLaneOffsetAction();
    state.followTrajectory = GetRunningFollowTrajectoryAction();
    state.assignRoute = GetRunningAssignRouteAction();
    state.longDistance = GetRunningLongDistanceAction();
    state.speedProfile = GetRunningSpeedProfileAction();
    state.synchronize = GetRunningSynchronizeAction();
    return state;
}

ControllerPythonDriver::ActionFlags ControllerPythonDriver::ToActionFlags(const RunningActionState& state)
{
    ActionFlags flags;
    flags.laneChanging = (state.laneChange != nullptr);
    flags.laneOffsetting = (state.laneOffset != nullptr);
    flags.followingTrajectory = (state.followTrajectory != nullptr);
    flags.assigningRoute = (state.assignRoute != nullptr);
    flags.longitudinalDistance = (state.longDistance != nullptr);
    flags.speedProfile = (state.speedProfile != nullptr);
    flags.synchronize = (state.synchronize != nullptr);
    return flags;
}

bool ControllerPythonDriver::HandlePathActions(
    const RunningActionState& state, const ActionFlags& previousFlags, const char* phaseLabel)
{
    bool pathActionStarted = false;

    if (!previousFlags.followingTrajectory && state.followTrajectory)
    {
        LOG_INFO("PythonDriverController: {}FollowTrajectory detected", phaseLabel);
        RegenerateWaypointsForTrajectory(state.followTrajectory);
        waypointsExtracted_ = true;
        state.followTrajectory->End();
        pathActionStarted = true;
    }

    if (!pathActionStarted && !previousFlags.laneChanging && state.laneChange && state.laneChange->target_)
    {
        const int targetLaneId = state.laneChange->target_->value_;
        const double duration = state.laneChange->transition_.GetParamTargetVal();
        LOG_INFO("PythonDriverController: {}LaneChange detected, target lane={}, duration={:.1f}s",
                 phaseLabel, targetLaneId, duration);
        RegenerateWaypointsForLaneChange(targetLaneId, duration);
        waypointsExtracted_ = true;
        state.laneChange->End();
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

        LOG_INFO("PythonDriverController: {}LaneOffset detected, target offset={:.2f}m, transition distance={:.1f}m",
                 phaseLabel, targetOffset, transitionDistance);
        RegenerateWaypointsForLaneOffset(targetOffset, transitionDistance);
        waypointsExtracted_ = true;
        state.laneOffset->End();
        pathActionStarted = true;
    }

    if (!pathActionStarted && !previousFlags.assigningRoute && state.assignRoute)
    {
        LOG_INFO("PythonDriverController: {}AssignRoute detected, refreshing route waypoints", phaseLabel);
        ExtractWaypoints("AssignRoute action");
        waypointsExtracted_ = true;
        state.assignRoute->End();
        pathActionStarted = true;
    }

    return pathActionStarted;
}

void ControllerPythonDriver::EvaluateScenarioActions()
{
    const RunningActionState state = GetRunningActionState();
    const ActionFlags previousFlags{
        wasLaneChanging_,
        wasLaneOffsetting_,
        wasFollowingTrajectory_,
        wasAssigningRoute_,
        wasLongitudinalDistance_,
        wasSpeedProfile_,
        wasSynchronize_
    };

    HandlePathActions(state, previousFlags, "");

    const ActionFlags currentFlags = ToActionFlags(state);

    if (!previousFlags.longitudinalDistance && currentFlags.longitudinalDistance)
    {
        LOG_INFO("PythonDriverController: LongitudinalDistanceAction detected");
    }
    if (!previousFlags.speedProfile && currentFlags.speedProfile)
    {
        LOG_INFO("PythonDriverController: SpeedProfileAction detected");
    }
    if (!previousFlags.synchronize && currentFlags.synchronize)
    {
        LOG_INFO("PythonDriverController: Synchronize detected");
    }

    frame_action_context_ = {};
    frame_action_context_.assignRoute = currentFlags.assigningRoute;
    frame_action_context_.laneChange = currentFlags.laneChanging;
    if (state.laneChange && state.laneChange->target_)
    {
        frame_action_context_.laneChangeTargetLane = state.laneChange->target_->value_;
        frame_action_context_.hasLaneChangeTargetLane = true;
    }
    frame_action_context_.laneOffset = currentFlags.laneOffsetting;
    if (state.laneOffset)
    {
        const double currentOffset = object_ ? object_->pos_.GetOffset() : 0.0;
        frame_action_context_.laneOffsetTargetM = ResolveLaneOffsetTarget(*state.laneOffset, currentOffset);
        frame_action_context_.hasLaneOffsetTargetM = true;
    }
    frame_action_context_.followTrajectory = currentFlags.followingTrajectory;
    frame_action_context_.longitudinalDistance = currentFlags.longitudinalDistance;
    frame_action_context_.speedProfile = currentFlags.speedProfile;
    frame_action_context_.synchronize = currentFlags.synchronize;

    wasLaneChanging_ = currentFlags.laneChanging;
    wasLaneOffsetting_ = currentFlags.laneOffsetting;
    wasFollowingTrajectory_ = currentFlags.followingTrajectory;
    wasAssigningRoute_ = currentFlags.assigningRoute;
    wasLongitudinalDistance_ = currentFlags.longitudinalDistance;
    wasSpeedProfile_ = currentFlags.speedProfile;
    wasSynchronize_ = currentFlags.synchronize;
}

void ControllerPythonDriver::UpdateVehiclePhysics(double timeStep)
{
    // When already stopped at end-of-road, skip physics entirely to prevent position creep
    // (Python PID would produce throttle targeting setSpeed_, causing small position advances each frame)
    if (object_ && real_vehicle_.speed_ <= 0.0 &&
        (object_->pos_.GetStatusBitMask() &
         static_cast<int>(roadmanager::Position::PositionStatusMode::POS_STATUS_END_OF_ROAD)))
    {
        real_vehicle_.speed_ = 0.0;
        currentSpeed_ = 0.0;
        return;
    }

    real_vehicle_.SetEngineBrakeFactor(input_.engineBrake);

    double terrain_pitch = 0.0;
    double terrain_roll  = 0.0;
    if (object_ && TerrainTracker::IsEnabled())
    {
        terrain_pitch = object_->pos_.GetP();
        terrain_roll  = object_->pos_.GetR();
    }
    real_vehicle_.SetTerrainAttitude(terrain_pitch, terrain_roll);

    real_vehicle_.UpdatePhysics(timeStep, input_.throttle, input_.brake, input_.steering, input_.gear);

    // Detect end-of-road and stop vehicle (matching DefaultController behavior)
    if (object_ && (object_->pos_.GetStatusBitMask() &
                    static_cast<int>(roadmanager::Position::PositionStatusMode::POS_STATUS_END_OF_ROAD)))
    {
        real_vehicle_.speed_ = 0.0;
    }

    currentSpeed_ = real_vehicle_.speed_;
}

void ControllerPythonDriver::UpdateCachedPowertrain()
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

void ControllerPythonDriver::UpdateHostVehicleReporter() const
{
    if (object_)
    {
        GT_HostVehicleReporter::Instance().SetBaseHostVehicleData(object_->GetId(), cached_hvd_);
    }
}

void ControllerPythonDriver::UpdateVehicleLights()
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

    ApplyLightPatch();

    auto set_light = [&](VehicleLightType type, bool on) {
        LightState state;
        state.mode = on ? LightState::Mode::ON : LightState::Mode::OFF;
        ext->SetLightState(type, state);
    };

    for (std::size_t i = 0; i < static_cast<std::size_t>(ControllerLightSlot::COUNT); ++i)
    {
        if (!light_runtime_[i].manual_override)
        {
            continue;
        }
        set_light(SlotToVehicleLight(static_cast<ControllerLightSlot>(i)), light_runtime_[i].manual_on);
    }
}

void ControllerPythonDriver::ApplyLightPatch()
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

    for (std::size_t i = 0; i < static_cast<std::size_t>(ControllerLightSlot::COUNT); ++i)
    {
        const int patch_value = input_.lights[i];
        if (patch_value == kLightUnset)
        {
            continue;
        }

        const auto slot = static_cast<ControllerLightSlot>(i);
        if (patch_value == kLightAuto)
        {
            light_runtime_[i].manual_override = false;
            ext->SetManualOverride(SlotToVehicleLight(slot), false);
        }
        else if (patch_value == kLightOff || patch_value == kLightOn)
        {
            light_runtime_[i].manual_override = true;
            light_runtime_[i].manual_on = (patch_value == kLightOn);
            ext->SetManualOverride(SlotToVehicleLight(slot), true);
        }
        else
        {
            LOG_WARN("PythonDriverController: invalid light patch value {} at index {}", patch_value, i);
        }

        // Consume per-frame patch (unspecified = no-op on following frames).
        input_.lights[i] = kLightUnset;
    }
}

int ControllerPythonDriver::BuildLightMaskFromExtension() const
{
    if (!object_)
    {
        return 0;
    }

    auto* vehicle = dynamic_cast<scenarioengine::Vehicle*>(object_);
    if (!vehicle)
    {
        return 0;
    }

    auto* ext = VehicleExtensionManager::Instance().GetExtension(vehicle);
    if (!ext)
    {
        return 0;
    }

    auto is_on = [&](VehicleLightType type) -> bool {
        return ext->GetLightState(type).mode == LightState::Mode::ON;
    };

    int mask = 0;
    if (is_on(VehicleLightType::LOW_BEAM)) mask |= 1;
    if (is_on(VehicleLightType::HIGH_BEAM)) mask |= 2;
    if (is_on(VehicleLightType::INDICATOR_LEFT)) mask |= 4;
    if (is_on(VehicleLightType::INDICATOR_RIGHT)) mask |= 8;
    if (is_on(VehicleLightType::FOG_LIGHTS) || is_on(VehicleLightType::FOG_LIGHTS_FRONT) || is_on(VehicleLightType::FOG_LIGHTS_REAR)) mask |= 16;
    return mask;
}

void ControllerPythonDriver::SyncObjectPoseFromRealVehicle()
{
    if (!object_)
    {
        return;
    }

    double dx, dy, dz_unused;
    real_vehicle_.GetBodyPositionOffset(dx, dy, dz_unused);

    // CRITICAL FIX: Normalize heading to [-π, π] before writing to esmini object
    // real_vehicle_.heading_ should already be normalized by UpdatePhysics(), but we normalize
    // again here as a safety measure to ensure esmini always receives correct heading range
    double normalized_heading = real_vehicle_.heading_;
    while (normalized_heading > M_PI) normalized_heading -= 2.0 * M_PI;
    while (normalized_heading < -M_PI) normalized_heading += 2.0 * M_PI;

    const double h    = normalized_heading;
    const double w_dx = dx * std::cos(h) - dy * std::sin(h);
    const double w_dy = dx * std::sin(h) + dy * std::cos(h);

    object_->pos_.SetInertiaPos(real_vehicle_.posX_ + w_dx, real_vehicle_.posY_ + w_dy, normalized_heading);
    object_->SetDirtyBits(scenarioengine::Object::DirtyBit::LATERAL | scenarioengine::Object::DirtyBit::LONGITUDINAL);
}

void ControllerPythonDriver::SyncGatewayObjectState(double combinedPitch, double combinedRoll)
{
    if (!object_ || !gateway_)
    {
        return;
    }

    double dx, dy, dz;
    real_vehicle_.GetBodyPositionOffset(dx, dy, dz);

    // CRITICAL FIX: Normalize heading to [-π, π] before writing to gateway
    double normalized_heading = real_vehicle_.heading_;
    while (normalized_heading > M_PI) normalized_heading -= 2.0 * M_PI;
    while (normalized_heading < -M_PI) normalized_heading += 2.0 * M_PI;

    const double h    = normalized_heading;
    const double w_dx = dx * std::cos(h) - dy * std::sin(h);
    const double w_dy = dx * std::sin(h) + dy * std::cos(h);

    gateway_->updateObjectWorldPosXYH(object_->id_, 0.0, real_vehicle_.posX_ + w_dx, real_vehicle_.posY_ + w_dy, normalized_heading);
    gateway_->updateObjectSpeed(object_->id_, 0.0, real_vehicle_.speed_);
    lastWrittenGatewaySpeed_ = real_vehicle_.speed_;
    gateway_->updateObjectWheelAngle(object_->id_, 0.0, real_vehicle_.wheelAngle_);
    gateway_->updateObjectWorldPos(
        object_->id_,
        0.0,
        real_vehicle_.posX_ + w_dx,
        real_vehicle_.posY_ + w_dy,
        real_vehicle_.posZ_ + dz,
        normalized_heading,
        combinedPitch,
        combinedRoll);

    SyncObjectPoseFromRealVehicle();
}

void ControllerPythonDriver::UpdateCurrentWaypointIndex()
{
    if (waypoints_.empty())
    {
        currentWaypointIndex_ = 0;
        return;
    }

    const int n = static_cast<int>(waypoints_.size());
    const int prev = std::clamp(currentWaypointIndex_, 0, n - 1);
    const int search_back = 20;
    const int search_fwd = 120;
    const int begin = std::max(0, prev - search_back);
    const int end = std::min(n - 1, prev + search_fwd);

    double dx, dy, dz_unused;
    real_vehicle_.GetBodyPositionOffset(dx, dy, dz_unused);
    const double h = real_vehicle_.heading_;
    const double x = real_vehicle_.posX_ + (dx * std::cos(h) - dy * std::sin(h));
    const double y = real_vehicle_.posY_ + (dx * std::sin(h) + dy * std::cos(h));

    double best_d2 = std::numeric_limits<double>::max();
    int best_idx = prev;
    for (int i = begin; i <= end; ++i)
    {
        const double wx = waypoints_[i].x;
        const double wy = waypoints_[i].y;
        const double d2 = (x - wx) * (x - wx) + (y - wy) * (y - wy);
        if (d2 < best_d2)
        {
            best_d2 = d2;
            best_idx = i;
        }
    }

    currentWaypointIndex_ = best_idx;
}

void ControllerPythonDriver::EnsureWaypointsExtracted()
{
    if (!waypointsExtracted_)
    {
        ExtractWaypoints();
        waypointsExtracted_ = true;
    }
}

void ControllerPythonDriver::ExtractWaypoints(const char* reason)
{
    waypoints_.clear();
    currentWaypointIndex_ = 0;

    if (!object_)
    {
        return;
    }

    roadmanager::Route* route = object_->pos_.GetRoute();
    lastObservedRoute_ = route;

    const double total_dist = realdetail::kWaypointTotalDistance;

    if (route)
    {
        // Route exists: step along the route using SetPathS() to get
        // road/s coordinates that correctly follow the assigned route
        // through junctions.  We manually apply the route waypoint
        // direction (GetRouteWaypointDir) to flip lane_id and offset
        // when the route goes against the road reference direction.
        // This mirrors what SetRouteLanePosition does when the
        // "align_routepositions" option is enabled.
        const double route_length = route->GetLength();
        const double route_s_start = object_->pos_.GetRouteS();
        const double route_s_end = std::min(route_s_start + total_dist, route_length);
        const int    lane_id = object_->pos_.GetLaneId();
        const double lane_offset = object_->pos_.GetOffset();

        // Build road_id → direction map from route waypoints so we can
        // correctly flip lane_id when the route goes against a road's
        // reference direction.  GetWaypoint()->GetRouteWaypointDir()
        // returns the direction of the last-passed waypoint, not of
        // the current road, so we need our own lookup.
        std::unordered_map<int, int> road_dir_map;
        for (size_t i = 0; i < route->all_waypoints_.size(); ++i)
        {
            auto& rwp = route->all_waypoints_[i];
            road_dir_map[rwp.GetTrackId()] = rwp.GetRouteWaypointDir();
        }

        // Save route state so our traversal doesn't disturb it.
        const double saved_path_s = route->GetPathS();

        roadmanager::Position pos;
        for (double rs = route_s_start; rs <= route_s_end; )
        {
            route->SetPathS(rs);
            int road_id = route->GetTrackId();
            double track_s = route->GetTrackS();

            // Look up the direction for this road from route waypoints.
            // For junction roads not in the map, default to dir=1.
            auto it = road_dir_map.find(road_id);
            int dir = (it != road_dir_map.end()) ? it->second : 1;
            int sign = (dir >= 0) ? 1 : -1;

            pos.SetLanePos(road_id, sign * lane_id, track_s, sign * lane_offset);
            pos.SetHeadingRelative(dir >= 0 ? 0.0 : M_PI);
            waypoints_.push_back(MakeWaypointFromPosition(pos, pos.GetOffset()));

            const double step = DetermineAdaptiveStep(pos);
            rs += step;
        }

        // Restore route state.
        route->SetPathS(saved_path_s);
    }
    else
    {
        // No route: step forward along the road using MoveAlongS() which
        // follows road successor links automatically.
        roadmanager::Position pos = object_->pos_;
        double d = 0.0;
        while (d < total_dist)
        {
            waypoints_.push_back(MakeWaypointFromPosition(pos, pos.GetOffset()));
            const double step = DetermineAdaptiveStep(pos);
            roadmanager::Position::ReturnCode rc = pos.MoveAlongS(step);
            if (static_cast<int>(rc) < 0)
            {
                break;
            }
            d += step;
        }
    }

    waypointGenerationVersion_++;
    if (reason && reason[0] != '\0')
    {
        LOG_INFO(
            "PythonDriverController: Waypoint refresh (reason='{}', source='{}', count={}, generation={})",
            reason,
            route ? "route_dense" : "forward_path",
            waypoints_.size(),
            waypointGenerationVersion_);
    }
}

void ControllerPythonDriver::RefreshWaypointsOnRoutePointerChange()
{
    if (!object_)
    {
        return;
    }

    const roadmanager::Route* currentRoute = object_->pos_.GetRoute();
    if (currentRoute != nullptr && currentRoute != lastObservedRoute_)
    {
        ExtractWaypoints("AssignRoute route pointer changed");
        waypointsExtracted_ = true;
    }
    lastObservedRoute_ = currentRoute;
}

void ControllerPythonDriver::RegenerateWaypointsForLaneOffset(double targetOffset, double transitionDistance)
{
    waypoints_.clear();
    currentWaypointIndex_ = 0;

    if (!object_)
    {
        return;
    }

    // Normalize heading before using for waypoint generation
    double normalized_heading = real_vehicle_.heading_;
    while (normalized_heading > M_PI) normalized_heading -= 2.0 * M_PI;
    while (normalized_heading < -M_PI) normalized_heading += 2.0 * M_PI;

    roadmanager::Position posBase;
    posBase.SetInertiaPosMode(real_vehicle_.posX_, real_vehicle_.posY_, normalized_heading,
                              roadmanager::Position::PosMode::H_ABS);
    const double startOffset = posBase.GetOffset();
    const double totalDist = realdetail::kWaypointTotalDistance;
    const double distForTransition = std::max(transitionDistance, 1.0);

    for (double d = 0.0; d < totalDist;)
    {
        const double step = DetermineAdaptiveStep(posBase);
        const double progress = std::min(d / distForTransition, 1.0);
        const double factor = realdetail::SmootherStep(progress);
        const double laneOffset = startOffset + (targetOffset - startOffset) * factor;

        roadmanager::Position offsetPos;
        offsetPos.SetLanePos(posBase.GetTrackId(), posBase.GetLaneId(), posBase.GetS(), laneOffset);
        waypoints_.push_back(MakeWaypointFromPosition(offsetPos, laneOffset));

        roadmanager::Position::ReturnCode rc = posBase.MoveAlongS(step);
        if (static_cast<int>(rc) < 0)
        {
            break;
        }
        d += step;
    }

    waypointGenerationVersion_++;
}

void ControllerPythonDriver::RegenerateWaypointsForTrajectory(scenarioengine::FollowTrajectoryAction* action)
{
    waypoints_.clear();
    currentWaypointIndex_ = 0;

    if (!action || !action->traj_ || !action->traj_->shape_)
    {
        LOG_WARN("PythonDriverController: FollowTrajectory action has no valid trajectory shape");
        return;
    }

    const double length = std::max(action->traj_->GetLength(), realdetail::kWaypointStep);
    for (double s = 0.0; s <= length;)
    {
        roadmanager::TrajVertex tv;
        if (action->traj_->shape_->Evaluate(s, roadmanager::Shape::TrajectoryParamType::TRAJ_PARAM_TYPE_S, tv) != 0)
        {
            s += 2.0;
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

        roadmanager::Position probe;
        probe.SetInertiaPosMode(tv.x, tv.y, tv.h_true, roadmanager::Position::PosMode::H_ABS);
        s += DetermineAdaptiveStep(probe);
    }

    if (waypoints_.empty())
    {
        ExtractWaypoints();
        return;
    }

    waypointGenerationVersion_++;
}

void ControllerPythonDriver::RegenerateWaypointsForLaneChange(int targetLaneId, double transitionDuration)
{
    waypoints_.clear();
    currentWaypointIndex_ = 0;

    if (!object_)
    {
        return;
    }

    const double speed = std::max(object_->GetSpeed(), 5.0);
    const double transitionDist = speed * std::max(transitionDuration, 0.1);
    const double totalDist = realdetail::kWaypointTotalDistance;

    // Normalize heading before using for waypoint generation
    double normalized_heading = real_vehicle_.heading_;
    while (normalized_heading > M_PI) normalized_heading -= 2.0 * M_PI;
    while (normalized_heading < -M_PI) normalized_heading += 2.0 * M_PI;

    roadmanager::Position posBase;
    posBase.SetInertiaPosMode(real_vehicle_.posX_, real_vehicle_.posY_, normalized_heading,
                              roadmanager::Position::PosMode::H_ABS);
    const int currentLaneId = posBase.GetLaneId();

    for (double d = 0.0; d < totalDist;)
    {
        const double step = DetermineAdaptiveStep(posBase);
        const double progress = std::min(d / transitionDist, 1.0);
        const double factor = realdetail::SmootherStep(progress);

        roadmanager::Position posCur;
        roadmanager::Position posTgt;
        posCur.SetLanePos(posBase.GetTrackId(), currentLaneId, posBase.GetS(), 0.0);
        posTgt.SetLanePos(posBase.GetTrackId(), targetLaneId, posBase.GetS(), 0.0);

        WaypointData wp;
        wp.x = posCur.GetX() * (1.0 - factor) + posTgt.GetX() * factor;
        wp.y = posCur.GetY() * (1.0 - factor) + posTgt.GetY() * factor;
        const double hCur = posCur.GetH();
        const double hTgt = posTgt.GetH();
        wp.h = hCur + factor * realdetail::NormalizeAngle(hTgt - hCur);
        wp.roadId = static_cast<uint32_t>(posBase.GetTrackId());
        wp.s = posBase.GetS();
        wp.laneId = targetLaneId;

        const double lateralDist = realdetail::Distance2D(posCur.GetX(), posCur.GetY(), posTgt.GetX(), posTgt.GetY());
        const double sign = (currentLaneId < targetLaneId) ? -1.0 : 1.0;
        wp.laneOffset = lateralDist * sign * (1.0 - factor);
        waypoints_.push_back(wp);

        roadmanager::Position::ReturnCode rc = posBase.MoveAlongS(step);
        if (static_cast<int>(rc) < 0)
        {
            break;
        }
        d += step;
    }

    waypointGenerationVersion_++;
}

void ControllerPythonDriver::FailAndStop(const std::string& message)
{
    if (fatal_error_)
    {
        return;
    }
    fatal_error_ = true;

    LOG_ERROR("{}", message);
    SE_LogMessage(message.c_str());
    SE_Close();
}

}  // namespace gt_esmini

#endif  // GT_ENABLE_EMBEDDED_PYTHON
