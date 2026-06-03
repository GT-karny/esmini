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
#include "gt_esmini/control/virtualdriver/ManeuverAwareSpeedPlanner.hpp"
#include "gt_esmini/control/virtualdriver/PIDPurePursuitDriver.hpp"
#include "gt_esmini/control/virtualdriver/AutoIndicatorPolicy.hpp"
#include "gt_esmini/core/ConfigLoader.hpp"
#include "gt_esmini/osi/GT_HostVehicleReporter.hpp"
#include "gt_esmini/scenario/ExtraEntities.hpp"
#include "CommonMini.hpp"
#include "Entities.hpp"
#include "OSCPrivateAction.hpp"
#include "RoadManager.hpp"
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
    midlong_planner_  = new ManeuverAwareSpeedPlanner(vd_config_.MidLongConfig());
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
    delete midlong_planner_;
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

    // 2b. Mid/long planner: scan the route ahead for a v_target(s) speed CEILING
    // (curvature, junction turns, speed limits) shaped by comfort deceleration so
    // the car slows *before* a curve/turn instead of arriving too fast and
    // saturating the steering. This is a pure upper bound: the short planner takes
    // min(commanded, ceiling), so the SpeedAction latch above is untouched.
    MidLongContext mctx;
    mctx.object    = object_;
    mctx.sim_time  = sim_time_;
    mctx.scan_dist = vd_config_.scan_distance;
    MidLongPlannerSnapshot midsnap = midlong_planner_->Plan(mctx);

    // Self-localize from the physics backend, not object->pos_. During a lateral
    // action (LaneChange/LaneOffset) the storyboard rewrites object->pos_ to the
    // *intended* pose every frame before this controller steps (ADDITIVE mode does
    // not early-return that action), so reading pos_ would make the driver believe
    // it is already on the intended path and never correct the physical ego's
    // cross-track lag. The backend pose is the true ego; the planner's preview is
    // the (absolute) intent, so the driver closes the loop on the real deviation.
    // x/y/h/speed must come from one source to keep the vehicle-frame transform
    // consistent. Read it ONCE here (before planning, so the control-point offset
    // can be gated on speed) and reuse it for the driver state below. Fall back to
    // object->pos_ for backends that own no model.
    double     phys_x, phys_y, phys_z, phys_h, phys_speed;
    const bool have_phys   = physics_backend_->GetPose(phys_x, phys_y, phys_z, phys_h, phys_speed);
    const double ego_speed = have_phys ? phys_speed : object_->GetSpeed();
    const double wheel_base = object_->boundingbox_.dimensions_.length_ * 0.6;

    // Forward control-point offset (P2 issue 2). Resolve the configured value:
    //   > 0  explicit distance [m] ahead of the origin
    //   = 0  AUTO — the front-axle distance (wheel_base); enabled by default
    //   < 0  disabled — keep the origin (≈ rear) reference (Phase 1 behavior)
    // Only while moving forward (stop/reverse keep the origin). The short planner
    // applies it to the preview anchor (and clamps it to 0 during a lateral
    // maneuver), then echoes the value it used so the driver state shifts to match.
    double requested_cp = 0.0;
    {
        const double cp = vd_config_.control_point_offset;
        const double dist = (cp < 0.0) ? 0.0 : (cp == 0.0 ? wheel_base : cp);
        if (ego_speed > vd_config_.control_point_min_speed)
            requested_cp = dist;
    }

    // 3. Auto pipeline: short planner -> driver model
    ShortPlanContext sctx;
    sctx.object               = object_;
    sctx.sim_time             = sim_time_;
    sctx.horizon_s            = vd_config_.horizon_s;
    sctx.dt                   = vd_config_.short_dt;
    sctx.v_target             = midsnap.valid ? &midsnap : nullptr;
    sctx.fallback_speed       = target_speed;
    sctx.control_point_offset = requested_cp;
    ShortPlannerSnapshot plan = short_planner_->Plan(sctx);

    DriverState dstate;
    if (have_phys)
    {
        dstate.x     = phys_x;
        dstate.y     = phys_y;
        dstate.h     = phys_h;
        dstate.speed = phys_speed;
    }
    else
    {
        dstate.x     = object_->pos_.GetX();
        dstate.y     = object_->pos_.GetY();
        dstate.h     = object_->pos_.GetH();
        dstate.speed = object_->GetSpeed();
    }
    dstate.wheel_base = wheel_base;

    // Shift the control point forward by EXACTLY the offset the planner applied to
    // the preview anchor (0 during a lateral maneuver / when disabled). Keeping the
    // two on the same route point is the hard-won invariant: the driver then nulls
    // a front-vs-front cross-track error, so the front tracks the lane on a turn.
    const double cp_used = plan.control_point_offset;
    dstate.x += cp_used * std::cos(dstate.h);
    dstate.y += cp_used * std::sin(dstate.h);

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

    // 4b. Manual indicator (turn-signal) control from input-source buttons,
    // via ManualDrive's auto-cancel FSM. When the human arms an indicator this
    // overrides the auto (maneuver-driven) policy below.
    const uint32_t in_buttons  = frame.pedal_steer ? frame.pedal_steer->buttons : 0u;
    const double   in_steering = frame.pedal_steer ? frame.pedal_steer->steering : 0.0;
    constexpr double kIndicatorCancelAngle = 0.3;  // normalized steering threshold
    const bool hazard_on = (in_buttons & ButtonBits::HAZARD) != 0;
    IndicatorFSM::Output manual_ind = indicator_fsm_.Update(
        in_buttons, prev_buttons_, in_steering, prev_steering_, kIndicatorCancelAngle, hazard_on);
    prev_buttons_  = in_buttons;
    prev_steering_ = in_steering;

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

    // 10. Indicator policy -> lights.
    // Auto maneuver direction: an active lane change takes priority; otherwise
    // look ahead along the route for a junction turn (pre-arm before the
    // intersection). Manual button input overrides the auto logic.
    IndicatorContext ictx;
    ictx.object       = object_;
    int maneuver_dir  = DetectManeuverDir(plan);
    if (maneuver_dir == 0)
        maneuver_dir = DetectJunctionTurn(dstate.speed);
    ictx.maneuver_dir = maneuver_dir;
    ictx.sim_time     = sim_time_;
    ictx.manual_left  = manual_ind.left_on;
    ictx.manual_right = manual_ind.right_on;
    ictx.manual_active = manual_ind.left_on || manual_ind.right_on;
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
    telemetry_.midlong               = midsnap;
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

int ControllerVirtualDriver::DetectJunctionTurn(double speed) const
{
    roadmanager::OpenDrive* odr = roadmanager::Position::GetOpenDrive();
    if (!odr) return 0;

    // Walk a copy of the ego route forward; find the first real (non-junction)
    // road reached *after* passing through a junction connecting road, and
    // compare its driving direction to the current one. Mirrors
    // ControllerRouteDrive::JunctionTurnDirection() but scans the route via
    // MoveAlongS (VirtualDriver has no precomputed waypoints_).
    roadmanager::Position pos;
    pos.Duplicate(object_->pos_);
    pos.CopyRoute(object_->pos_);

    const double cur_h     = object_->pos_.GetDrivingDirection();
    const id_t   cur_track = object_->pos_.GetTrackId();

    // Lead-time based lookahead so the signal pre-arms before the intersection.
    const double lookahead = std::max(15.0, speed * vd_config_.indicator_lead_time + 10.0);
    const double step      = 2.0;
    double       traveled  = 0.0;
    bool         passed_junction = false;

    while (traveled < lookahead)
    {
        int ret = static_cast<int>(pos.MoveAlongS(step));
        if (ret == static_cast<int>(roadmanager::Position::ReturnCode::ERROR_GENERIC))
            break;
        traveled += step;

        const id_t t = pos.GetTrackId();
        if (t == cur_track) continue;

        roadmanager::Road* r = odr->GetRoadById(t);
        if (!r) continue;

        if (r->GetJunction() != ID_UNDEFINED)
        {
            passed_junction = true;  // on a junction connecting road
            continue;
        }
        if (!passed_junction)
            return 0;  // direct successor road (no junction) = straight continuation

        const double diff = GetAngleInIntervalMinusPIPlusPI(pos.GetDrivingDirection() - cur_h);
        constexpr double threshold = 0.10;  // rad (matches RouteDrive / AutoLight)
        if (diff > threshold)  return +1;  // left
        if (diff < -threshold) return -1;  // right
        return 0;
    }
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
