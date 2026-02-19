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

    currentSpeed_       = object_->GetSpeed();
    setSpeed_           = object_->GetSpeed();
    lastObservedRoute_  = object_->pos_.GetRoute();
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

    const double objectSpeed = object_->GetSpeed();
    if (std::abs(objectSpeed - currentSpeed_) > 1e-3)
    {
        setSpeed_     = objectSpeed;
        currentSpeed_ = objectSpeed;
    }
}

void ControllerPythonDriver::UpdateVehiclePhysics(double timeStep)
{
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
    const double h    = real_vehicle_.heading_;
    const double w_dx = dx * std::cos(h) - dy * std::sin(h);
    const double w_dy = dx * std::sin(h) + dy * std::cos(h);
    object_->pos_.SetInertiaPos(real_vehicle_.posX_ + w_dx, real_vehicle_.posY_ + w_dy, real_vehicle_.heading_);
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
    const double h    = real_vehicle_.heading_;
    const double w_dx = dx * std::cos(h) - dy * std::sin(h);
    const double w_dy = dx * std::sin(h) + dy * std::cos(h);

    gateway_->updateObjectWorldPosXYH(object_->id_, 0.0, real_vehicle_.posX_ + w_dx, real_vehicle_.posY_ + w_dy, real_vehicle_.heading_);
    gateway_->updateObjectSpeed(object_->id_, 0.0, real_vehicle_.speed_);
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

void ControllerPythonDriver::EnsureWaypointsExtracted()
{
    if (!waypointsExtracted_)
    {
        ExtractWaypoints();
        waypointsExtracted_ = true;
    }
}

void ControllerPythonDriver::ExtractWaypoints()
{
    waypoints_.clear();
    currentWaypointIndex_ = 0;

    if (!object_)
    {
        return;
    }

    roadmanager::Route* route = object_->pos_.GetRoute();
    if (!route)
    {
        lastObservedRoute_ = nullptr;

        roadmanager::Position pos = object_->pos_;
        const double total_dist = realdetail::kWaypointTotalDistance;
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
        return;
    }

    lastObservedRoute_ = route;
    const std::vector<roadmanager::Position>& routeWaypoints = route->all_waypoints_;
    for (const auto& wp : routeWaypoints)
    {
        waypoints_.push_back(MakeWaypointFromPosition(wp, wp.GetOffset()));
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
        ExtractWaypoints();
        waypointsExtracted_ = true;
    }
    lastObservedRoute_ = currentRoute;
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
