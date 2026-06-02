#include "gt_esmini/control/ControllerVirtualDriver.hpp"
#include "gt_esmini/control/manualdrive/IInputSource.hpp"
#include "gt_esmini/control/common/IPhysicsBackend.hpp"
#include "gt_esmini/control/common/RealVehicleBackend.hpp"
#include "gt_esmini/control/manualdrive/StubInputSource.hpp"
#include "gt_esmini/control/manualdrive/NetworkInputBridge.hpp"
#ifdef GT_ENABLE_SDL2
#include "gt_esmini/control/manualdrive/SDL2WheelInput.hpp"
#endif
#include "gt_esmini/control/virtualdriver/TrajectoryShortPlanner.hpp"
#include "gt_esmini/control/virtualdriver/PIDPurePursuitDriver.hpp"
#include "gt_esmini/control/virtualdriver/AutoIndicatorPolicy.hpp"
#include "gt_esmini/core/ConfigLoader.hpp"
#include "gt_esmini/osi/GT_HostVehicleReporter.hpp"
#include "gt_esmini/scenario/ExtraEntities.hpp"
#include "CommonMini.hpp"
#include "Entities.hpp"
#include "OSCPrivateAction.hpp"
#include "logger.hpp"

#include <cmath>

namespace gt_esmini { extern std::string GetCurrentModuleDirectory(); }

using namespace scenarioengine;

namespace gt_esmini
{

namespace
{
// Evaluate an OpenSCENARIO speed TransitionDynamics shape at progress p in [0,1].
double EvalSpeedShape(OSCPrivateAction::DynamicsShape shape, double v0, double dv, double p)
{
    p = std::min(1.0, std::max(0.0, p));
    switch (shape)
    {
        case OSCPrivateAction::DynamicsShape::SINUSOIDAL: return v0 - dv * (std::cos(M_PI * p) - 1.0) / 2.0;
        case OSCPrivateAction::DynamicsShape::CUBIC:      return v0 + dv * p * p * (3.0 - 2.0 * p);
        case OSCPrivateAction::DynamicsShape::LINEAR:     return v0 + dv * p;
        case OSCPrivateAction::DynamicsShape::STEP:       return v0 + dv;
        default:                                          return v0 + dv;
    }
}
}  // namespace

ControllerVirtualDriver::ControllerVirtualDriver(InitArgs* args)
    : Controller(args)
{
    // --- Load config ---
    std::string exe_dir = GetCurrentModuleDirectory();
    ConfigLoader loader;

    std::string config_filename = "virtual_driver.json";
    if (args && args->properties && args->properties->ValueExists("ConfigFile"))
        config_filename = args->properties->GetValueStr("ConfigFile");

    std::string config_path;
    if (!config_filename.empty() && (config_filename[0] == '/' || (config_filename.size() > 1 && config_filename[1] == ':')))
        config_path = config_filename;
    else
        config_path = loader.ResolveConfigPath(exe_dir, config_filename);

    if (!vd_config_.LoadFromFile(config_path))
        LOG_INFO("VirtualDriverController: Config not found at {}, using defaults", config_path);

    // --- Build the ManualDrive-style IO config for the reused input/override layer ---
    io_config_.input_type                 = vd_config_.input_type;
    io_config_.input_network.transport_type = vd_config_.input_transport;
    io_config_.input_network.port         = vd_config_.input_port;
    io_config_.override_cfg.enabled        = vd_config_.override_enabled;
    io_config_.override_cfg.button_override = vd_config_.override_button;
    io_config_.override_cfg.steering_threshold = vd_config_.steering_threshold;
    io_config_.override_cfg.throttle_threshold = vd_config_.throttle_threshold;
    io_config_.override_cfg.brake_threshold    = vd_config_.brake_threshold;
    io_config_.override_cfg.auto_return_timeout = vd_config_.auto_return_timeout;
    io_config_.domain.lateral      = vd_config_.override_lateral;
    io_config_.domain.longitudinal = vd_config_.override_longitudinal;

    // --- Create input source (reused ManualDrive sources) ---
#ifdef GT_ENABLE_SDL2
    if (vd_config_.input_type == "sdl2_wheel")
        input_source_ = new SDL2WheelInput();
    else
#endif
    if (vd_config_.input_type == "network")
        input_source_ = new NetworkInputBridge();
    else
        input_source_ = new StubInputSource();

    // --- Create pluggable layers ---
    physics_backend_  = new RealVehicleBackend();
    short_planner_    = new TrajectoryShortPlanner(vd_config_.ShortPlannerConfig());
    driver_model_     = new PIDPurePursuitDriver(vd_config_.DriverConfig());
    indicator_policy_ = new AutoIndicatorPolicy(vd_config_.IndicatorConfig());

    override_mgr_.Configure(io_config_);

    // Run ADDITIVE (like ManualDrive): the storyboard keeps setting the targets
    // (SpeedAction -> object speed, LaneChangeAction visible via getPrivateActions),
    // which we read each frame and realize through physics, overwriting pos_.
    // Under OVERRIDE the SpeedAction would not set object speed, so the driver
    // would have no target.
    if (args && args->properties && !args->properties->ValueExists("mode"))
        mode_ = ControlOperationMode::MODE_ADDITIVE;

    LOG_INFO("VirtualDriverController: Created (input={}, planner=trajectory, driver=pid_pure_pursuit)",
             vd_config_.input_type);
}

ControllerVirtualDriver::~ControllerVirtualDriver()
{
    if (input_source_)
    {
        input_source_->Shutdown();
        delete input_source_;
    }
    delete physics_backend_;
    delete short_planner_;
    delete driver_model_;
    delete indicator_policy_;
}

void ControllerVirtualDriver::Init()
{
    operating_domains_ = static_cast<unsigned int>(ControlDomainMasks::DOMAIN_MASK_LAT_AND_LONG);
    if (mode_ != ControlOperationMode::MODE_ADDITIVE)
    {
        LOG_INFO("VirtualDriverController mode \"{}\" not applicable. Using additive mode.", Mode2Str(mode_));
        mode_ = ControlOperationMode::MODE_ADDITIVE;
    }
    Controller::Init();
}

int ControllerVirtualDriver::Activate(const ControlActivationMode (&mode)[static_cast<unsigned int>(ControlDomains::COUNT)])
{
    if (object_)
    {
        PhysicsInitParams params = vd_config_.PhysicsParams();
        physics_backend_->Init(params, object_);
        physics_backend_->SetInitialState(
            object_->pos_.GetX(), object_->pos_.GetY(), object_->pos_.GetZ(),
            object_->pos_.GetH(), object_->GetSpeed());

        input_source_->Init(io_config_);

        // Register VehicleLightExtension (same pattern as ManualDrive / RealDriver).
        if (auto* vehicle = dynamic_cast<scenarioengine::Vehicle*>(object_))
        {
            auto* ext = VehicleExtensionManager::Instance().GetExtension(vehicle);
            if (!ext)
            {
                ext = new VehicleLightExtension(vehicle);
                VehicleExtensionManager::Instance().RegisterExtension(vehicle, ext);
            }
        }

        LOG_INFO("VirtualDriverController: Activated for object {} at ({:.1f}, {:.1f})",
                 object_->GetId(), object_->pos_.GetX(), object_->pos_.GetY());
    }
    return Controller::Activate(mode);
}

void ControllerVirtualDriver::Deactivate()
{
    LOG_INFO("VirtualDriverController: Deactivated");
    Controller::Deactivate();
}

void ControllerVirtualDriver::Step(double timeStep)
{
    if (!object_) return;
    sim_time_ += timeStep;

    // Teleport: re-sync physics to the new pose (fresh dynamic state).
    if (object_->dirty_.Check(static_cast<uint64_t>(scenarioengine::Object::DirtyBit::TELEPORT)))
    {
        physics_backend_->SetInitialState(
            object_->pos_.GetX(), object_->pos_.GetY(), object_->pos_.GetZ(),
            object_->pos_.GetH(), object_->GetSpeed());
    }

    // 1. Poll input + override decision
    InputFrame frame = input_source_->Poll(timeStep);
    override_mgr_.Update(frame, timeStep);
    const bool lat_manual = override_mgr_.IsLateralManual();
    const bool lon_manual = override_mgr_.IsLongitudinalManual();

    // 2. Scenario target speed (read from the running SpeedAction; latched).
    const double target_speed = ResolveTargetSpeed();

    // 3. Auto pipeline: short planner -> driver model
    ShortPlanContext sctx;
    sctx.object         = object_;
    sctx.sim_time       = sim_time_;
    sctx.horizon_s      = vd_config_.horizon_s;
    sctx.dt             = vd_config_.short_dt;
    sctx.v_target       = nullptr;          // Phase 1: no mid/long planner yet
    sctx.fallback_speed = target_speed;
    ShortPlannerSnapshot plan = short_planner_->Plan(sctx);

    DriverState dstate;
    dstate.x          = object_->pos_.GetX();
    dstate.y          = object_->pos_.GetY();
    dstate.h          = object_->pos_.GetH();
    dstate.speed      = object_->GetSpeed();
    dstate.wheel_base = object_->boundingbox_.dimensions_.length_ * 0.6;
    DriverModelSnapshot dsnap;
    PedalSteerCommand   auto_cmd = driver_model_->Compute(plan, dstate, timeStep, &dsnap);

    // 4. Merge manual override per domain
    PedalSteerCommand cmd = auto_cmd;
    if (frame.pedal_steer)
    {
        const PedalSteerCommand& m = *frame.pedal_steer;
        if (lat_manual) cmd.steering = m.steering;
        if (lon_manual) { cmd.throttle = m.throttle; cmd.brake = m.brake; }
        cmd.buttons             = m.buttons;
        cmd.gear                = m.gear;
        cmd.paddle_up_pressed   = m.paddle_up_pressed;
        cmd.paddle_down_pressed = m.paddle_down_pressed;
    }
    last_cmd_ = cmd;

    // 5. Physics step
    osi3::HostVehicleData hvd = physics_backend_->StepPedalSteer(cmd, timeStep);

    // 6. Extract resolved vehicle state from HVD
    double pos_x = 0.0, pos_y = 0.0, pos_z = 0.0, heading = 0.0, speed = 0.0, wheel_angle = 0.0;
    if (hvd.has_location())
    {
        const auto& loc = hvd.location();
        if (loc.has_position())    { pos_x = loc.position().x(); pos_y = loc.position().y(); pos_z = loc.position().z(); }
        if (loc.has_orientation()) { heading = loc.orientation().yaw(); }
        if (loc.has_velocity())    { speed = std::sqrt(std::pow(loc.velocity().x(), 2) + std::pow(loc.velocity().y(), 2)); }
    }
    if (hvd.has_vehicle_steering() && hvd.vehicle_steering().has_vehicle_steering_wheel())
        wheel_angle = hvd.vehicle_steering().vehicle_steering_wheel().angle();

    // 7. Body offset + attitude
    double body_dx = 0.0, body_dy = 0.0, body_dz = 0.0;
    physics_backend_->GetBodyPositionOffset(body_dx, body_dy, body_dz);
    double combined_pitch = 0.0, combined_roll = 0.0;
    physics_backend_->GetDynamicAttitude(combined_pitch, combined_roll);

    // 8. Apply to object — physics owns pos_ and speed (block_speed = false).
    state_applier_.Apply(object_, pos_x, pos_y, pos_z, heading, speed, wheel_angle,
                         body_dx, body_dy, body_dz, combined_pitch, combined_roll,
                         /*block_speed_update=*/false);
    physics_backend_->SyncRoadZ(object_->pos_.GetZ());

    // 9. OSI HostVehicleReporter
    current_hvd_ = hvd;
    GT_HostVehicleReporter::Instance().SetBaseHostVehicleData(object_->GetId(), hvd);

    // 10. Indicator policy -> lights
    IndicatorContext ictx;
    ictx.object       = object_;
    ictx.maneuver_dir = DetectManeuverDir(plan);
    ictx.sim_time     = sim_time_;
    IndicatorSnapshot ind = indicator_policy_->Update(ictx, timeStep);
    ApplyLights(cmd, ind);

    // 11. Telemetry
    telemetry_.sim_time              = sim_time_;
    telemetry_.x                     = object_->pos_.GetX();
    telemetry_.y                     = object_->pos_.GetY();
    telemetry_.z                     = object_->pos_.GetZ();
    telemetry_.h                     = object_->pos_.GetH();
    telemetry_.speed                 = object_->GetSpeed();
    telemetry_.track_id              = static_cast<int>(object_->pos_.GetTrackId());
    telemetry_.lane_id               = object_->pos_.GetLaneId();
    telemetry_.lane_offset           = object_->pos_.GetOffset();
    telemetry_.s                     = object_->pos_.GetS();
    telemetry_.override_lateral      = lat_manual;
    telemetry_.override_longitudinal = lon_manual;
    telemetry_.short_plan            = plan;
    telemetry_.driver                = dsnap;
    telemetry_.indicator             = ind;

    // 12. Base controller step
    scenarioengine::Controller::Step(timeStep);
}

double ControllerVirtualDriver::ResolveTargetSpeed()
{
    if (!target_initialized_)
    {
        last_target_speed_   = object_->GetSpeed();  // seed with initial speed
        last_action_target_  = last_target_speed_;
        target_initialized_  = true;
    }

    // Find a running absolute-speed action (the engine no longer applies it to
    // object speed once a controller owns the LONG domain, so we drive it).
    LongSpeedAction* sa = nullptr;
    for (auto* action : object_->getPrivateActions())
    {
        if (action->action_type_ == OSCAction::ActionType::LONG_SPEED &&
            action->GetCurrentState() == StoryBoardElement::State::RUNNING)
        {
            sa = static_cast<LongSpeedAction*>(action);
            break;
        }
    }

    if (sa && sa->target_)
    {
        // Capture only the start *time* once per action; the speeds and the
        // (time-normalized) duration come from esmini's transition, which
        // SpeedAction::Start fills under MODE_ADDITIVE.
        if (speed_action_id_ != sa)
        {
            speed_action_id_ = sa;
            speed_start_t_   = sim_time_;
        }

        const OSCPrivateAction::TransitionDynamics& td = sa->transition_;
        const double v0  = td.GetStartVal();        // speed at action start
        const double vt  = td.GetTargetVal();        // target speed (perf-clamped)
        const double dur = td.GetParamTargetVal();   // duration [s]; distance/rate pre-converted

        double v_ref;
        if (dur < 1e-6 || td.shape_ == OSCPrivateAction::DynamicsShape::STEP)
        {
            v_ref = vt;  // step change
        }
        else
        {
            const double p = std::min(1.0, std::max(0.0, (sim_time_ - speed_start_t_) / dur));
            v_ref = EvalSpeedShape(td.shape_, v0, vt - v0, p);
        }
        last_target_speed_  = v_ref;
        last_action_target_ = vt;  // remember the commanded endpoint
    }
    else
    {
        // No action running: snap to the most recent action's TARGET, not the last
        // mid-transition value. This finishes a deceleration whose action was
        // cancelled early (e.g. a gradual stop overwritten by an instantaneous
        // step-to-0 that completes before we observe it).
        speed_action_id_   = nullptr;
        last_target_speed_ = last_action_target_;
    }
    return last_target_speed_;
}

int ControllerVirtualDriver::DetectManeuverDir(const ShortPlannerSnapshot& plan) const
{
    // Only signal when a lane change is actually in progress (avoid curve
    // false-positives). Direction from the far preview point's lateral offset
    // in the vehicle frame: +y (local) = left.
    bool lane_change_active = false;
    for (auto* action : object_->getPrivateActions())
    {
        if (action->action_type_ == OSCAction::ActionType::LAT_LANE_CHANGE &&
            action->GetCurrentState() == StoryBoardElement::State::RUNNING)
        {
            lane_change_active = true;
            break;
        }
    }
    if (!lane_change_active || plan.preview.size() < 2) return 0;

    const TrajectoryPoint& far_pt = plan.preview.back();
    const double h  = object_->pos_.GetH();
    const double dx = far_pt.x - object_->pos_.GetX();
    const double dy = far_pt.y - object_->pos_.GetY();
    const double local_y = -dx * std::sin(h) + dy * std::cos(h);

    if (local_y > 0.5)  return +1;  // left
    if (local_y < -0.5) return -1;  // right
    return 0;
}

void ControllerVirtualDriver::ApplyLights(const PedalSteerCommand& cmd, const IndicatorSnapshot& ind)
{
    auto* vehicle = dynamic_cast<scenarioengine::Vehicle*>(object_);
    if (!vehicle) return;
    auto* ext = VehicleExtensionManager::Instance().GetExtension(vehicle);
    if (!ext) return;

    auto set_light = [&](VehicleLightType type, bool on) {
        if (ext->IsScenarioControlled(type)) return;  // scenario has priority
        LightState ls;
        ls.mode = on ? LightState::Mode::ON : LightState::Mode::OFF;
        ext->SetLightState(type, ls);
        ext->SetLightSource(type, LightSource::MANUAL_DRIVE);
    };

    set_light(VehicleLightType::BRAKE_LIGHTS,    cmd.brake > 0.05);
    set_light(VehicleLightType::REVERSING_LIGHTS, cmd.gear == -1);
    set_light(VehicleLightType::INDICATOR_LEFT,  ind.left_on);
    set_light(VehicleLightType::INDICATOR_RIGHT, ind.right_on);
}

void ControllerVirtualDriver::GetInputsForOSI(double& throttle, double& brake, double& steering, int& gear, int& lightMask) const
{
    throttle = last_cmd_.throttle;
    brake    = last_cmd_.brake;
    if (current_hvd_.has_vehicle_steering() && current_hvd_.vehicle_steering().has_vehicle_steering_wheel())
        steering = current_hvd_.vehicle_steering().vehicle_steering_wheel().angle();
    else
        steering = 0.0;
    if (current_hvd_.has_vehicle_powertrain())
        gear = current_hvd_.vehicle_powertrain().gear_transmission();
    else
        gear = last_cmd_.gear;
    lightMask = BuildLightMaskFromExtension();
}

void ControllerVirtualDriver::GetPowertrainForOSI(double& rpm, double& torque) const
{
    if (current_hvd_.has_vehicle_powertrain() && current_hvd_.vehicle_powertrain().motor_size() > 0)
    {
        rpm    = current_hvd_.vehicle_powertrain().motor(0).rpm();
        torque = current_hvd_.vehicle_powertrain().motor(0).torque();
    }
    else
    {
        rpm    = 0.0;
        torque = 0.0;
    }
}

int ControllerVirtualDriver::BuildLightMaskFromExtension() const
{
    if (!object_ || object_->type_ != scenarioengine::Object::Type::VEHICLE) return 0;
    auto* vehicle = static_cast<scenarioengine::Vehicle*>(object_);
    auto* ext = VehicleExtensionManager::Instance().GetExtension(vehicle);
    if (!ext) return 0;

    auto is_on = [&](VehicleLightType type) {
        return ext->GetLightState(type).mode == LightState::Mode::ON;
    };

    int mask = 0;
    if (is_on(VehicleLightType::LOW_BEAM))         mask |= 1;
    if (is_on(VehicleLightType::HIGH_BEAM))        mask |= 2;
    if (is_on(VehicleLightType::INDICATOR_LEFT))   mask |= 4;
    if (is_on(VehicleLightType::INDICATOR_RIGHT))  mask |= 8;
    if (is_on(VehicleLightType::FOG_LIGHTS) ||
        is_on(VehicleLightType::FOG_LIGHTS_FRONT) ||
        is_on(VehicleLightType::FOG_LIGHTS_REAR))  mask |= 16;
    return mask;
}

scenarioengine::Controller* InstantiateControllerVirtualDriver(void* args)
{
    auto* initArgs = static_cast<scenarioengine::Controller::InitArgs*>(args);
    return new ControllerVirtualDriver(initArgs);
}

}  // namespace gt_esmini
