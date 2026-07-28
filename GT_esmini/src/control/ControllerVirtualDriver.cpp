#include "gt_esmini/control/ControllerVirtualDriver.hpp"
#include "gt_esmini/control/common/JunctionTurn.hpp"
#include "gt_esmini/control/common/ModuleDirectory.hpp"
#include "gt_esmini/control/common/TransitionDynamics.hpp"
#include "gt_esmini/control/manualdrive/IFFBSink.hpp"
#include "gt_esmini/control/manualdrive/IInputSource.hpp"
#include "gt_esmini/control/common/IPhysicsBackend.hpp"
#include "gt_esmini/control/common/RealVehicleBackend.hpp"
#include "gt_esmini/control/manualdrive/HeadlessFfbInput.hpp"
#include "gt_esmini/control/manualdrive/StubInputSource.hpp"
#include "gt_esmini/control/manualdrive/NetworkInputBridge.hpp"
#ifdef GT_ENABLE_SDL2
#include "gt_esmini/control/manualdrive/SDL2WheelInput.hpp"
#endif
#include "gt_esmini/control/virtualdriver/TrajectoryShortPlanner.hpp"
#include "gt_esmini/control/virtualdriver/ManeuverAwareSpeedPlanner.hpp"
#include "gt_esmini/control/virtualdriver/PIDPurePursuitDriver.hpp"
#include "gt_esmini/control/virtualdriver/AdSteeringEnvelope.hpp"
#include "gt_esmini/control/virtualdriver/ResumeMergeProfile.hpp"
#include "gt_esmini/control/virtualdriver/AutoIndicatorPolicy.hpp"
#include "gt_esmini/control/virtualdriver/TrafficPolicyManager.hpp"
#include "gt_esmini/control/virtualdriver/policies/LeadVehicleAware.hpp"
#include "gt_esmini/control/virtualdriver/policies/TrafficLightAware.hpp"
#include "gt_esmini/control/virtualdriver/policies/StopYieldSignAware.hpp"
#include "gt_esmini/control/virtualdriver/policies/ConflictPointResolver.hpp"
#include "gt_esmini/control/virtualdriver/policies/CrosswalkPedestrianAware.hpp"
#include "gt_esmini/control/virtualdriver/policies/AebSafety.hpp"
#include "gt_esmini/core/ConfigLoader.hpp"
#include "gt_esmini/osi/GT_HostVehicleReporter.hpp"
#include "gt_esmini/scenario/ExtraEntities.hpp"
#include "CommonMini.hpp"
#include "Entities.hpp"
#include "OSCPrivateAction.hpp"
#include "RoadManager.hpp"
#include "logger.hpp"

#include <cmath>
#include <memory>

using namespace scenarioengine;

namespace gt_esmini
{

ControllerVirtualDriver::ControllerVirtualDriver(InitArgs* args)
    : Controller(args)
{
    // --- Load config ---
    std::string exe_dir = GetCurrentModuleDirectory();
    ConfigLoader loader;

    std::string config_filename = "virtual_driver.json";
    if (args && args->properties && args->properties->ValueExists("ConfigFile"))
        config_filename = args->properties->GetValueStr("ConfigFile");

    // Absolute ConfigFile (web backend per-run config) passes through; a bare
    // filename resolves relative to this module's config/ dir (audit F5).
    std::string config_path = loader.ResolveConfigPathOrPassthrough(exe_dir, config_filename);

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
    // SDL2 wheel button bindings (only consumed when input_type=="sdl2_wheel").
    io_config_.sdl2.override_button        = vd_config_.sdl2_override_button;
    io_config_.sdl2.indicator_left_button  = vd_config_.sdl2_indicator_left_button;
    io_config_.sdl2.indicator_right_button = vd_config_.sdl2_indicator_right_button;
    io_config_.sdl2.upshift_button         = vd_config_.sdl2_upshift_button;
    io_config_.sdl2.downshift_button       = vd_config_.sdl2_downshift_button;
    io_config_.sdl2.headlight_button       = vd_config_.sdl2_headlight_button;
    io_config_.sdl2.high_beam_button       = vd_config_.sdl2_high_beam_button;
    io_config_.sdl2.fog_light_button       = vd_config_.sdl2_fog_light_button;
    io_config_.sdl2.hazard_button          = vd_config_.sdl2_hazard_button;
    io_config_.sdl2.auto_resume_button     = vd_config_.sdl2_auto_resume_button;  // feature:F7

    // feature:F7 (F7b) — FFB target-track config propagates from VD flat keys
    // into the shared ManualDriveConfig struct that SDLFFBSink + OverrideManager
    // both read. Default enabled=false → existing VD behavior unchanged.
    io_config_.ffb.target_track.enabled                        = vd_config_.ffb_target_track_enabled;
    io_config_.ffb.target_track.kp                             = vd_config_.ffb_target_track_kp;
    io_config_.ffb.target_track.kd                             = vd_config_.ffb_target_track_kd;
    io_config_.ffb.target_track.max_force                      = vd_config_.ffb_target_track_max_force;
    io_config_.ffb.target_track.hard_stop_zone                 = vd_config_.ffb_target_track_hard_stop_zone;
    io_config_.ffb.target_track.friction_ff                    = vd_config_.ffb_target_track_friction_ff;
    io_config_.ffb.target_track.friction_ff_eps                = vd_config_.ffb_target_track_friction_ff_eps;
    io_config_.ffb.target_track.feel_ratio                     = vd_config_.ffb_target_track_feel_ratio;
    io_config_.ffb.target_track.override_steer_force_threshold = vd_config_.ffb_target_track_override_steer_force_threshold;
    io_config_.ffb.target_track.override_steer_dev_threshold   = vd_config_.ffb_target_track_override_steer_dev_threshold;
    io_config_.ffb.target_track.override_sustain_time          = vd_config_.ffb_target_track_override_sustain_time;
    io_config_.ffb.target_track.override_target_rate_gate         = vd_config_.ffb_target_track_override_target_rate_gate;
    io_config_.ffb.target_track.override_position_error_rate_gate = vd_config_.ffb_target_track_override_position_error_rate_gate;
    io_config_.ffb.target_track.override_residual_threshold        = vd_config_.ffb_target_track_override_residual_threshold;
    io_config_.ffb.target_track.override_residual_reanchor_tau     = vd_config_.ffb_target_track_override_residual_reanchor_tau;
    io_config_.ffb.target_track.override_shadow_breakaway          = vd_config_.ffb_target_track_override_shadow_breakaway;
    io_config_.ffb.target_track.override_shadow_breakaway_left     = vd_config_.ffb_target_track_override_shadow_breakaway_left;
    io_config_.ffb.target_track.override_shadow_breakaway_right    = vd_config_.ffb_target_track_override_shadow_breakaway_right;
    io_config_.ffb.target_track.override_shadow_motion_epsilon     = vd_config_.ffb_target_track_override_shadow_motion_epsilon;
    io_config_.ffb.target_track.override_shadow_kinetic            = vd_config_.ffb_target_track_override_shadow_kinetic;
    io_config_.ffb.target_track.override_shadow_force_to_velocity  = vd_config_.ffb_target_track_override_shadow_force_to_velocity;
    io_config_.ffb.target_track.override_shadow_v_max              = vd_config_.ffb_target_track_override_shadow_v_max;
    io_config_.ffb.target_track.override_shadow_velocity_tau       = vd_config_.ffb_target_track_override_shadow_velocity_tau;
    io_config_.ffb.target_track.override_shadow_dead_time          = vd_config_.ffb_target_track_override_shadow_dead_time;
    io_config_.ffb.target_track.override_shadow_onset_grace        = vd_config_.ffb_target_track_override_shadow_onset_grace;
    io_config_.ffb.target_track.override_shadow_motion_rate_eps    = vd_config_.ffb_target_track_override_shadow_motion_rate_eps;
    io_config_.ffb.safety.max_saturation_seconds                   = vd_config_.ffb_safety_max_saturation_seconds;
    io_config_.ffb.safety.max_runtime_seconds                      = vd_config_.ffb_safety_max_runtime_seconds;
    io_config_.ffb.safety.saturation_ratio                         = vd_config_.ffb_safety_saturation_ratio;

    // feature:F7 — AD steering safety envelope (see AdSteeringEnvelope.hpp).
    // Built once here; config is not hot-reloaded during a run.
    ad_envelope_cfg_ = vd_config_.AdEnvelopeConfig();

    // feature:F7 resume-merge (see ResumeMergeProfile.hpp). Built once here,
    // same convention as ad_envelope_cfg_ above; not hot-reloaded during a run.
    resume_merge_cfg_ = vd_config_.ResumeMergeCfg();

    // --- Create input source (reused ManualDrive sources) ---
#ifdef GT_ENABLE_SDL2
    if (vd_config_.input_type == "sdl2_wheel")
        input_source_ = new SDL2WheelInput();
    else
#endif
    if (vd_config_.input_type == "network")
        input_source_ = new NetworkInputBridge();
    else if (vd_config_.input_type == "headless_ffb")
        // feature:F7 (F7b) — synthetic-wheel + synthetic-FFB source for the
        // headless closed-loop regression smoke (vd_ffb_headless_smoke.py).
        // Exists ONLY to exercise the servo-to-override-manager wiring on CI
        // where no SDL2 wheel is plugged in. Not intended for scenario runs.
        input_source_ = new HeadlessFfbInput();
    else
        input_source_ = new StubInputSource();

    // --- Create pluggable layers ---
    physics_backend_  = new RealVehicleBackend();
    short_planner_    = new TrajectoryShortPlanner(vd_config_.ShortPlannerConfig());
    midlong_planner_  = new ManeuverAwareSpeedPlanner(vd_config_.MidLongConfig());
    driver_model_     = new PIDPurePursuitDriver(vd_config_.DriverConfig());
    indicator_policy_ = new AutoIndicatorPolicy(vd_config_.IndicatorConfig());

    // --- Phase 3 traffic policies: add only the enabled ones (config flags) ---
    traffic_policy_mgr_ = new TrafficPolicyManager();
    if (vd_config_.policy_lead_enabled)
        traffic_policy_mgr_->Add(std::make_unique<LeadVehicleAware>(vd_config_.LeadConfig()));
    if (vd_config_.policy_traffic_light_enabled)
        traffic_policy_mgr_->Add(std::make_unique<TrafficLightAware>(vd_config_.TrafficLightConfig()));
    if (vd_config_.policy_stop_yield_enabled)
        traffic_policy_mgr_->Add(std::make_unique<StopYieldSignAware>(vd_config_.StopYieldConfig()));
    if (vd_config_.policy_conflict_enabled)
        traffic_policy_mgr_->Add(std::make_unique<ConflictPointResolver>(vd_config_.ConflictConfig()));
    if (vd_config_.policy_crosswalk_enabled)
        traffic_policy_mgr_->Add(std::make_unique<CrosswalkPedestrianAware>(vd_config_.CrosswalkConfig()));
    if (vd_config_.policy_aeb_enabled)
        traffic_policy_mgr_->Add(std::make_unique<AebSafety>(vd_config_.AebConfig()));

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
    delete traffic_policy_mgr_;
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

void ControllerVirtualDriver::SetUpControlOutputs()
{
    if (!object_) return;

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

    // feature:F7 — see VirtualDriverTypes.hpp: the only telemetry field written
    // outside Step(), because it must survive the deactivation that stops Step()
    // from running at all.
    telemetry_.vd_active = true;
}

void ControllerVirtualDriver::TearDownControlOutputs()
{
    // feature:F7 — set first, not last: everything below can early-return
    // (no FFB sink) or log, but "control handed back" must be recorded
    // unconditionally the instant teardown begins.
    telemetry_.vd_active = false;

    // Force feedback must be released here and nowhere else: ScenarioEngine only
    // steps active controllers, so the moment this controller goes inactive our
    // Step() - and with it SDLFFBSink::Update() - stops being called. The device
    // holds the last commanded force as an infinite-duration constant effect, so
    // without this the wheel would keep pulling after the scenario took over.
    if (IFFBSink* ffb = input_source_ ? input_source_->GetFFBSink() : nullptr)
    {
        ffb->SetSteerTarget(0.0, false);
        ffb->SetEnabled(false);
    }

    // Drop any latched manual-intervention state. Once the scenario has taken
    // control away the latch describes nothing, and it cannot clear itself while
    // inactive (the idle timer only advances from Step()), so it would otherwise
    // be carried straight into the next activation.
    override_mgr_.RequestAutoMode();

    LOG_INFO("VirtualDriverController: control outputs released");
}

int ControllerVirtualDriver::Activate(const ControlActivationMode (&mode)[static_cast<unsigned int>(ControlDomains::COUNT)])
{
    const bool was_active = Active();

    // Apply the requested per-domain modes first, then react to the transition.
    // VirtualDriver sets neither align_to_road_heading_on_activation_ nor
    // ..._on_deactivation_, so the base call is pure bit manipulation and is safe
    // to evaluate before deciding what to set up or tear down.
    const int rc = Controller::Activate(mode);

    const bool is_active = Active();

    if (!was_active && is_active)
    {
        SetUpControlOutputs();
    }
    else if (was_active && !is_active)
    {
        // An ActivateControllerAction that switches every domain off never reaches
        // Deactivate() - upstream routes it through Activate() with OFF modes - so
        // this is the only place the scenario-driven handover is observable.
        TearDownControlOutputs();
    }
    // Staying active re-runs no initialisation: input_source_->Init() has no
    // multiple-call guard and would re-open the joystick and orphan the existing
    // haptic effects.

    return rc;
}

void ControllerVirtualDriver::Deactivate()
{
    if (Active())
    {
        TearDownControlOutputs();
    }
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

    // 1a. feature:F7 (F7b) FFB torque-proxy: feed OverrideManager last frame's
    // servo sample so the driver push-back can latch to MANUAL. Sample is
    // inert (active=false) unless the target-track servo is running (config
    // gate ffb.target_track.enabled + AD lateral, wired below in step 6).
    IFFBSink* ffb = input_source_ ? input_source_->GetFFBSink() : nullptr;
    if (ffb)
    {
        override_mgr_.UpdateFfbSample(ffb->GetInterventionSample());
    }

    override_mgr_.Update(frame, timeStep);
    const bool lat_manual = override_mgr_.IsLateralManual();
    const bool lon_manual = override_mgr_.IsLongitudinalManual();

    // 2. Scenario target speed (read from the running SpeedAction; latched).
    const double target_speed = ResolveTargetSpeed();

    // 2a. Traffic policies (Phase 3): evaluate the enabled policies (lead-vehicle /
    // traffic-light / stop-yield sign) into a set of speed/stop constraints. These
    // feed the mid/long planner below, which folds them into the v_target(s)
    // ceiling (strictest wins; STOP -> 0). Empty when no policy is enabled, so the
    // planner path is identical to Phase 2.
    TrafficPolicySnapshot policy_snap;
    if (traffic_policy_mgr_ && traffic_policy_mgr_->PolicyCount() > 0)
    {
        TrafficPolicyContext pctx;
        pctx.ego      = object_;
        pctx.entities = entities_;
        pctx.sim_time = sim_time_;
        policy_snap   = traffic_policy_mgr_->Evaluate(pctx);
    }

    // 2b. Mid/long planner: scan the route ahead for a v_target(s) speed CEILING
    // (curvature, junction turns, speed limits) shaped by comfort deceleration so
    // the car slows *before* a curve/turn instead of arriving too fast and
    // saturating the steering. This is a pure upper bound: the short planner takes
    // min(commanded, ceiling), so the SpeedAction latch above is untouched.
    // Policy constraints (2a) are folded into the same ceiling.
    MidLongContext mctx;
    mctx.object    = object_;
    mctx.sim_time  = sim_time_;
    mctx.scan_dist = vd_config_.scan_distance;
    mctx.policy    = &policy_snap;
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

    // 2c. feature:F7 resume-merge (docs/virtualdriver/resume_merge_trajectory_design.md).
    // Smooths a manual->AUTO_RESUME lateral hand-over by ramping a ROUTE-lane
    // reference into the short planner instead of the raw per-frame
    // current-lane snap (TrajectoryShortPlanner.cpp's anchor). Entirely gated
    // behind resume_merge_cfg_.enabled (shipped default: TRUE since
    // 2026-07-28; it was false while the feature was being validated) -- when
    // false, NOTHING below this guard executes, so merge_now_* keep the SAME
    // values ShortPlanContext already defaults its merge_* fields to, and
    // TrajectoryShortPlanner's pre-existing current-lane-anchor path runs
    // with no new arithmetic (HARD INVARIANT: bit-identical to today when
    // disabled).
    bool         merge_now_active      = false;
    unsigned int merge_now_track       = 0;
    int          merge_now_lane        = 0;
    double       merge_now_offset      = 0.0;
    const char*  merge_fallback_reason = "";

    if (resume_merge_cfg_.enabled)
    {
        // Route-lane resolution (design doc section 2-0-1), re-run every
        // frame (not just at the arming instant) so a mid-merge route loss is
        // caught by the disarm check below, and the planner always gets a
        // FRESH target lane rather than one captured once at arm time.
        unsigned int route_track = 0;
        int          route_lane  = 0;
        const char*  route_fail  = ResolveResumeMergeRouteLane(route_track, route_lane);
        const bool   route_ok    = route_fail[0] == '\0';
        merge_fallback_reason    = route_fail;

        // A running storyboard lateral maneuver (LaneChange/LaneOffset) takes
        // full ownership of the preview overlay in TrajectoryShortPlanner --
        // same RUNNING + action-type filter that planner uses to build its
        // own lat_actions, duplicated here because the CONTROLLER (not the
        // planner) owns the merge state machine's disarm decision.
        bool has_lateral_storyboard_action = false;
        for (auto* action : object_->getPrivateActions())
        {
            if (action->GetCurrentState() != StoryBoardElement::State::RUNNING) continue;
            if (action->action_type_ == OSCAction::ActionType::LAT_LANE_CHANGE ||
                action->action_type_ == OSCAction::ActionType::LAT_LANE_OFFSET)
            {
                has_lateral_storyboard_action = true;
                break;
            }
        }

        // Disarm (design doc section 8-3): storyboard lateral action, manual
        // re-latch, or route loss. Checked BEFORE a possible re-arm below so
        // a stale armed state can never survive past its own trigger frame.
        if (resume_merge_state_.active &&
            (has_lateral_storyboard_action || lat_manual || !route_ok))
        {
            DisarmResumeMerge(resume_merge_state_);
        }

        // Arm on the manual->AUTO_RESUME edge. override_mgr_.Update() (above)
        // already updated JustTransitionedToAuto() for this frame, so arming
        // can fire on the SAME frame the edge occurs (handoff section 2-7: no
        // one-frame lag; object_->pos_ is the true ego pose even under manual
        // override, since physics owns it every frame -- state_applier_.Apply()
        // below).
        if (!has_lateral_storyboard_action && route_ok && override_mgr_.JustTransitionedToAuto())
        {
            // Placed at the SAME s used to resolve route_track/route_lane
            // above (object_->pos_.GetS()), not the route's own internal
            // local_s -- they can differ slightly (route-boundary / virtual-
            // junction clamping), so guard this SetLanePos's return value too
            // rather than trust a silent success: consistent with design doc
            // section 2-0-1's overarching "never trust a silent SetLanePos
            // outcome" discipline, even though its literal step 6 does not
            // call this specific site out. On failure, route_center's X/Y
            // would be a freshly-constructed Position's defaults, not a real
            // road point -- skip arming rather than capture d0 from that.
            roadmanager::Position route_center;
            if (route_center.SetLanePos(route_track, route_lane, object_->pos_.GetS(), 0.0) !=
                roadmanager::Position::ReturnCode::ERROR_GENERIC)
            {
                const double h_road = route_center.GetHRoad();

                // Route-relative lateral deviation: project the ego -> route-
                // lane-centre displacement onto the +t axis (design doc
                // section 4-1 / handoff section 2-3). Deliberately NOT
                // pos_.GetOffset() -- that is LANE-relative and
                // re-references at lane boundaries (measured: -1.7482 ->
                // +1.9425 in a single frame).
                const double d0 = -(object_->pos_.GetX() - route_center.GetX()) * std::sin(h_road) +
                                    (object_->pos_.GetY() - route_center.GetY()) * std::cos(h_road);

                const double ego_h  = object_->pos_.GetH();
                const double v0_lat = object_->GetSpeed() * std::sin(ego_h - h_road);

                double a0_lat = 0.0;
                if (prev_heading_valid_ && timeStep > 1e-9)
                {
                    const double yaw_rate = GetAngleInIntervalMinusPIPlusPI(ego_h - prev_heading_) / timeStep;
                    a0_lat = yaw_rate * object_->GetSpeed();
                }

                ArmResumeMerge(resume_merge_state_, d0, v0_lat, a0_lat, resume_merge_cfg_);
            }
        }

        if (resume_merge_state_.active)
            AdvanceResumeMerge(resume_merge_state_, timeStep);

        if (resume_merge_state_.active)
        {
            merge_now_active = true;
            merge_now_track  = route_track;
            merge_now_lane   = route_lane;
            merge_now_offset = EvaluateResumeMergeOffset(resume_merge_state_, 0.0);
        }

        // Rolling one-frame-back heading, used ONLY to derive a0_lat at the
        // NEXT arming instant (design doc section 8-3(a)). Updated every
        // frame this feature is enabled (MANUAL or AUTO) so a hand-over
        // always sees the true realized heading one frame back, not a stale
        // AUTO-only sample.
        prev_heading_       = object_->pos_.GetH();
        prev_heading_valid_ = true;
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
    // feature:F7 resume-merge: defaults (false/0/0/0.0/nullptr) preserve
    // today's behavior when merge_now_active is false (disabled, never
    // armed, or disarmed this frame) -- see the HARD INVARIANT note above.
    sctx.merge_active     = merge_now_active;
    sctx.merge_track_id   = merge_now_track;
    sctx.merge_lane_id    = merge_now_lane;
    sctx.merge_offset_now = merge_now_offset;
    sctx.merge_state      = merge_now_active ? &resume_merge_state_ : nullptr;
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

    // 3a. feature:F7 AD steering safety envelope — clamp AD's raw command to
    // physical lateral-accel / yaw-rate / steering-rate limits BEFORE it
    // reaches the manual-override merge below or the FFB target servo (5a).
    // Pure Pursuit + TrajectoryShortPlanner's per-frame lane-center snap have
    // no lateral-deviation/rate/amplitude limit of their own — see
    // AdSteeringEnvelope.hpp. Independent of max_lateral_accel (that value
    // already shapes curve speed, so reusing it here would clamp during
    // ordinary curve driving). Overwriting auto_cmd.steering in place means
    // both the merge below and the FFB target at 5a see the clamped value
    // for free, with no further change needed at either site.
    AdSteeringEnvelopeSnapshot envelope_snap;
    auto_cmd.steering = ComputeAdSteeringEnvelope(
        auto_cmd.steering, dstate.speed, dstate.wheel_base, vd_config_.max_steer_angle,
        timeStep, ad_envelope_state_, ad_envelope_cfg_, &envelope_snap);

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

    // 4a. feature:F7 — persist WHATEVER steering command was actually realized
    // this frame (the envelope's own clamped AD output while AUTO, or the raw
    // manual input while MANUAL) as next frame's rate-limit anchor, AND the
    // realized rate this produced as next frame's jerk-limit anchor. This is
    // what lets a manual->AUTO_RESUME transition ramp smoothly from the
    // physical wheel angle (and its realized rate) instead of a stale AD
    // proposal, with no dedicated "resume ramp" state machine
    // (AdSteeringEnvelope.hpp).
    UpdateAdSteeringEnvelopeState(ad_envelope_state_, cmd.steering, timeStep);

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

    // 5a. feature:F7 (F7b) FFB target-track servo update. AD's commanded wheel
    // angle (auto_cmd.steering, normalized [-1..1] — already passed through the
    // steering envelope at 3a, so the servo never chases a pathological
    // target and err = target - actual stays small) is handed to the servo so
    // it drives the physical wheel to follow. active=true only when AD owns
    // lateral (lat_manual=false); the config master gate ffb.target_track.enabled
    // lives inside SDLFFBSink::SetSteerTarget so it always wins over active.
    // Order matters: SetSteerTarget BEFORE ffb->Update so the servo evaluates
    // against the fresh target this frame.
    if (ffb)
    {
        ffb->SetSteerTarget(auto_cmd.steering, /*active=*/!lat_manual);
        ffb->Update(hvd, timeStep);
    }

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
    int maneuver_dir  = DetectManeuverDir();
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
    telemetry_.manual_transition     = override_mgr_.JustTransitionedToManual();
    telemetry_.auto_transition       = override_mgr_.JustTransitionedToAuto();
    telemetry_.resume_pressed        = override_mgr_.JustPressedResume();
    // feature:F7 (F7b) FFB target-track observability. Sample this frame's
    // servo state (populated by ffb->Update above; inert when servo is off).
    if (ffb)
    {
        const FfbInterventionSample s   = ffb->GetInterventionSample();
        telemetry_.ffb_target_active    = s.active;
        telemetry_.ffb_commanded_force  = s.commanded_force;
        telemetry_.ffb_position_error   = s.position_error;
        telemetry_.ffb_target_norm      = s.target_norm;
        // The RAW sink force, recorded alongside the rest of this frame's
        // sample. gates.effective_force below is NOT a substitute: that is
        // OverrideManager's own diagnostic, so it is (a) one frame behind this
        // block and (b) the DEAD-TIME-DELAYED force the detector actually
        // consumed. The two coincide only while dead_time is 0, which is what
        // the pre-2026-07-26 recordings happen to have. Anything recorded
        // under the shipped defaults (dead_time=0.041) has a gates force that
        // is genuinely not this one, so a replay reconstructing the detector's
        // input from gates would silently feed it an already-delayed force and
        // delay it a second time. GT_esmini/test/tools/ffb_override_replay.cpp
        // prefers this field and falls back to the shifted gates force only
        // for the older fixtures that predate it.
        telemetry_.ffb_sample_effective_force = s.effective_force_signed;
    }
    else
    {
        telemetry_.ffb_target_active    = false;
        telemetry_.ffb_commanded_force  = 0.0;
        telemetry_.ffb_position_error   = 0.0;
        telemetry_.ffb_target_norm      = 0.0;
        telemetry_.ffb_sample_effective_force = 0.0;
    }
    // feature:F7 — override-latch diagnostics. Real-machine "why didn't it
    // fire" observability: without this, diagnosing a missed latch required
    // re-instrumenting the code on-site. The residual/shadow pair is the part
    // to read first. See OverrideManager::FfbLatchDiagnostics.
    {
        using BlockReason = OverrideManager::FfbLatchDiagnostics::BlockReason;
        const auto& diag = override_mgr_.GetFfbLatchDiagnostics();
        const char* reason_str = "none";
        switch (diag.block_reason)
        {
            case BlockReason::NONE:            reason_str = "none";            break;
            case BlockReason::INACTIVE:        reason_str = "inactive";        break;
            case BlockReason::BOOTSTRAP:       reason_str = "bootstrap";       break;
            case BlockReason::BELOW_RESIDUAL:  reason_str = "below_residual";  break;
        }

        telemetry_.ffb_gate_over_force           = diag.over_force;
        telemetry_.ffb_gate_over_dev             = diag.over_dev;
        telemetry_.ffb_gate_moving_target        = diag.moving_target;
        telemetry_.ffb_gate_tracking_transient   = diag.tracking_transient;
        telemetry_.ffb_gate_target_rate          = diag.target_rate;
        telemetry_.ffb_gate_derror_rate          = diag.derror_rate;
        telemetry_.ffb_gate_actual_norm          = diag.actual_norm;
        telemetry_.ffb_gate_shadow_norm          = diag.shadow_norm;
        telemetry_.ffb_gate_residual             = diag.residual;
        telemetry_.ffb_gate_residual_threshold   = diag.residual_threshold;
        telemetry_.ffb_gate_effective_force      = diag.effective_force;
        telemetry_.ffb_gate_shadow_moving        = diag.shadow_moving;
        telemetry_.ffb_gate_sustain_accum        = diag.sustain_accum;
        telemetry_.ffb_gate_sustain_time         = diag.sustain_time;
        telemetry_.ffb_gate_block_reason         = reason_str;

        // feature:F7 — re-anchor instrument (observational only; see
        // test_results/f7_reanchor_instrument_spec.md and
        // OverrideManager::FfbLatchDiagnostics::ReanchorSource).
        using ReanchorSource = OverrideManager::FfbLatchDiagnostics::ReanchorSource;
        const char* reanchor_reason_str = "none";
        switch (diag.reanchor_source)
        {
            case ReanchorSource::NONE:            reanchor_reason_str = "none";            break;
            case ReanchorSource::SEED:            reanchor_reason_str = "seed";            break;
            case ReanchorSource::ONSET_GRACE:     reanchor_reason_str = "onset_grace";     break;
            case ReanchorSource::DRIFT:           reanchor_reason_str = "drift";           break;
            case ReanchorSource::RESUME:          reanchor_reason_str = "resume";          break;
            case ReanchorSource::INACTIVE_REARM:  reanchor_reason_str = "inactive_rearm";  break;
            case ReanchorSource::SERVO_TRACKING:  reanchor_reason_str = "servo_tracking";  break;
        }

        telemetry_.ffb_gate_reanchor_hard_count           = diag.reanchor_hard_count;
        telemetry_.ffb_gate_reanchor_soft_count           = diag.reanchor_soft_count;
        telemetry_.ffb_gate_reanchor_delta                = diag.reanchor_delta;
        telemetry_.ffb_gate_reanchor_hard_delta_abs_accum = diag.reanchor_hard_delta_abs_accum;
        telemetry_.ffb_gate_reanchor_soft_delta_abs_accum = diag.reanchor_soft_delta_abs_accum;
        telemetry_.ffb_gate_reanchor_source               = reanchor_reason_str;
        telemetry_.ffb_gate_free_shadow_norm              = diag.free_shadow_norm;
        telemetry_.ffb_gate_free_residual                 = diag.free_residual;
        telemetry_.ffb_gate_free_below_real_count         = diag.free_below_real_count;
    }
    // feature:F7 — AD steering safety envelope observability (verification:
    // "normal driving never trips the envelope"). See AdSteeringEnvelope.hpp.
    telemetry_.ad_envelope_lateral_accel_active = envelope_snap.lateral_accel_active;
    telemetry_.ad_envelope_yaw_rate_active      = envelope_snap.yaw_rate_active;
    telemetry_.ad_envelope_steer_rate_active    = envelope_snap.steer_rate_active;
    telemetry_.ad_envelope_steer_jerk_active    = envelope_snap.steer_jerk_active;
    telemetry_.ad_envelope_active               = envelope_snap.any_active;
    // dsnap.steer (telemetry_.driver.steer, set via telemetry_.driver = dsnap
    // below) stays the RAW pre-envelope AD proposal — deliberately untouched.
    // These two are what the envelope actually saw/produced, so "did the
    // envelope change anything this frame" is observable from telemetry alone.
    telemetry_.ad_envelope_steer_in  = envelope_snap.steer_norm_in;
    telemetry_.ad_envelope_steer_out = envelope_snap.steer_norm_out;
    telemetry_.short_plan            = plan;
    telemetry_.midlong               = midsnap;
    telemetry_.policy                = policy_snap;
    telemetry_.driver                = dsnap;
    telemetry_.indicator             = ind;

    // feature:F7 resume-merge telemetry (design doc
    // resume_merge_trajectory_design.md section 8-6). Controller-owned merge
    // state-machine snapshot; deliberately NOT part of ShortPlannerSnapshot
    // (short_plan above), which stays a cross-session contract untouched by
    // this feature. resume_merge_state_'s captured fields (d0/v0_lat/a0_lat/
    // a_bound/duration_s/comfort_unmet) retain their last-armed values across
    // a disarm (see DisarmResumeMerge's own doc), so they stay readable here
    // as "what the last merge was" even the frame after it stops being active.
    telemetry_.resume_merge.active        = merge_now_active;
    telemetry_.resume_merge.d0            = resume_merge_state_.d0;
    telemetry_.resume_merge.v0_lat        = resume_merge_state_.v0_lat;
    telemetry_.resume_merge.a0_lat        = resume_merge_state_.a0_lat;
    telemetry_.resume_merge.a_bound       = resume_merge_state_.a_bound;
    telemetry_.resume_merge.comfort_unmet = resume_merge_state_.comfort_unmet;
    telemetry_.resume_merge.duration_s    = resume_merge_state_.duration_s;
    telemetry_.resume_merge.progress      = (resume_merge_state_.duration_s > 1e-9)
                                                 ? std::min(1.0, resume_merge_state_.elapsed_s / resume_merge_state_.duration_s)
                                                 : 0.0;
    telemetry_.resume_merge.target_offset = merge_now_offset;
    // "解決したルート車線（フォールバック時は現在車線と一致）" (design doc section
    // 8-6): report the CURRENT lane whenever the merge is not actually
    // steering the anchor this frame (disabled / not armed / disarmed /
    // route unresolved), so this pair always shows "what anchor is actually
    // in effect", not a stale or zeroed resolution attempt.
    telemetry_.resume_merge.route_track   = merge_now_active ? static_cast<int>(merge_now_track)
                                                               : static_cast<int>(object_->pos_.GetTrackId());
    telemetry_.resume_merge.route_lane    = merge_now_active ? merge_now_lane : object_->pos_.GetLaneId();
    telemetry_.resume_merge.fallback_reason = merge_fallback_reason;

    // 11b. Front-bumper (leading-edge) road localization (F5). Project the vehicle
    // origin forward by (length/2 + bbox center-x) along the heading — the same
    // front-offset LeadVehicleAware uses — then localize that world point on the
    // road. Uses a throwaway Position so the ego's own pos_ is untouched.
    {
        const double front_off = object_->boundingbox_.dimensions_.length_ / 2.0 +
                                 object_->boundingbox_.center_.x_;
        const double h  = object_->pos_.GetH();
        const double fx = object_->pos_.GetX() + front_off * std::cos(h);
        const double fy = object_->pos_.GetY() + front_off * std::sin(h);
        roadmanager::Position fb;
        const bool ok = (fb.SetInertiaPos(fx, fy, h) == 0);
        telemetry_.front_bumper.x       = fx;
        telemetry_.front_bumper.y       = fy;
        telemetry_.front_bumper.road_id = static_cast<int>(fb.GetTrackId());
        telemetry_.front_bumper.lane_id = fb.GetLaneId();
        telemetry_.front_bumper.s       = fb.GetS();
        telemetry_.front_bumper.t       = fb.GetT();
        telemetry_.front_bumper.offset  = fb.GetOffset();
        telemetry_.front_bumper.valid   = ok;
    }

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
            v_ref = EvaluateTransitionShape(td.shape_, v0, vt - v0, p);
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

int ControllerVirtualDriver::ResolveLaneChangeDir(const scenarioengine::LatLaneChangeAction* lc) const
{
    if (lc == nullptr || lc->target_ == nullptr) return 0;

    const int  current_lane = object_->pos_.GetLaneId();
    const bool along_s      = IsAngleForward(object_->pos_.GetHRelative());

    int target_lane = current_lane;
    if (lc->target_->type_ == scenarioengine::LatLaneChangeAction::Target::Type::ABSOLUTE_LANE)
    {
        target_lane = lc->target_->value_;
    }
    else  // RELATIVE_LANE -- mirrors LatLaneChangeAction::Start's own resolution
    {
        const scenarioengine::Object* ref =
            static_cast<const scenarioengine::LatLaneChangeAction::TargetRelative*>(lc->target_)->object_;
        if (ref == nullptr) return 0;
        target_lane = ref->pos_.GetLaneId() + lc->target_->value_ * (IsAngleForward(ref->pos_.GetHRelative()) ? 1 : -1);
    }

    return LaneChangeIndicatorDir(current_lane, target_lane, along_s);
}

int ControllerVirtualDriver::DetectManeuverDir()
{
    // Only signal when a lane change is actually in progress (avoid curve false-positives).
    const scenarioengine::LatLaneChangeAction* lc = nullptr;
    for (auto* action : object_->getPrivateActions())
    {
        if (action->action_type_ == OSCAction::ActionType::LAT_LANE_CHANGE &&
            action->GetCurrentState() == StoryBoardElement::State::RUNNING)
        {
            lc = static_cast<const scenarioengine::LatLaneChangeAction*>(action);
            break;
        }
    }

    if (lc == nullptr)
    {
        lane_change_action_id_ = nullptr;
        lane_change_dir_       = 0;
        return 0;
    }

    // Latch the direction at action start. Two reasons: (1) once the ego crosses the lane boundary its
    // current lane id becomes the target and the delta would collapse to 0, dropping the indicator
    // before the manoeuvre completes; (2) the direction is a property of the ACTION, not of the
    // instantaneous geometry. Same pointer-identity latch the SpeedAction handling uses above.
    if (lc != lane_change_action_id_)
    {
        lane_change_action_id_ = lc;
        lane_change_dir_       = ResolveLaneChangeDir(lc);
    }
    return lane_change_dir_;
}

int ControllerVirtualDriver::DetectJunctionTurn(double speed) const
{
    roadmanager::OpenDrive* odr = roadmanager::Position::GetOpenDrive();
    if (!odr) return 0;

    // Lead-time based lookahead so the signal pre-arms before the intersection.
    const double lookahead = std::max(15.0, speed * vd_config_.indicator_lead_time + 10.0);
    return RouteLookaheadJunctionTurnDirection(object_->pos_, odr, lookahead);
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

    // Debounce the brake light: the speed PID emits brake micro-pulses
    // (0 -> 0.2-0.3 -> 0 every ~0.15 s) while tracking the stepped speed reference,
    // which would flicker the light on/off ~tens of times per decel. Latch ON on
    // any brake and hold for 0.35 s past the last pulse so the gaps are bridged.
    // (sim_time_ is advanced at the top of Step, so it is current here.)
    if (cmd.brake > 0.05)
    {
        brake_light_on_         = true;
        brake_light_hold_until_ = sim_time_ + 0.35;
    }
    else if (sim_time_ >= brake_light_hold_until_)
    {
        brake_light_on_ = false;
    }
    set_light(VehicleLightType::BRAKE_LIGHTS,    brake_light_on_);
    set_light(VehicleLightType::REVERSING_LIGHTS, cmd.gear == -1);
    set_light(VehicleLightType::INDICATOR_LEFT,  ind.left_on);
    set_light(VehicleLightType::INDICATOR_RIGHT, ind.right_on);
}

const char* ControllerVirtualDriver::ResolveResumeMergeRouteLane(unsigned int& out_track, int& out_lane) const
{
    // feature:F7 resume-merge route-lane resolution (design doc
    // resume_merge_trajectory_design.md section 2-0-1). Isolated route clone
    // (pos.CopyRoute), same safe pattern JunctionTurn.hpp uses, so mutating
    // it via SetTrackS below never touches the shared Route* any other code
    // reads. BOTH OnRoute() and a track-id match against the ego's own
    // current track are required: Route::SetTrackS silently swallows
    // SetLanePos's ERROR_GENERIC (RoadManager.cpp:15514, return value
    // unchecked) and, off-route, silently leaves currentPos_ (and therefore
    // GetLaneId()) at its last-synced value (RoadManager.cpp:15419-15421,
    // 15508-15546) -- OnRoute() alone does not catch either failure mode.
    roadmanager::Position pos;
    pos.Duplicate(object_->pos_);
    pos.CopyRoute(object_->pos_);

    roadmanager::Route* route = pos.GetRoute();
    if (!route || !route->IsValid())
        return "no_route";

    const id_t ego_track = object_->pos_.GetTrackId();
    route->SetTrackS(ego_track, object_->pos_.GetS());

    if (!route->OnRoute())
        return "off_route";
    if (route->GetTrackId() != ego_track)
        return "track_mismatch";

    out_track = route->GetTrackId();
    out_lane  = route->GetLaneId();
    return "";
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

void ControllerVirtualDriver::GetADASFunctions(std::vector<AdasFunctionState>& functions) const
{
    // The enable flags come from config (which policies were instantiated at
    // all) and the per-frame states from the last evaluated policy snapshot, so
    // "disabled" stays distinguishable from "armed but quiet" — see
    // BuildAdasFunctionReport().
    VdPolicyEnableFlags flags;
    flags.lead          = vd_config_.policy_lead_enabled;
    flags.traffic_light = vd_config_.policy_traffic_light_enabled;
    flags.stop_yield    = vd_config_.policy_stop_yield_enabled;
    flags.conflict      = vd_config_.policy_conflict_enabled;
    flags.crosswalk     = vd_config_.policy_crosswalk_enabled;
    flags.aeb           = vd_config_.policy_aeb_enabled;

    functions = BuildAdasFunctionReport(flags, telemetry_.policy);
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

    // R5-U3: read straight from native storage via the bridge (no extension needed).
    auto is_on = [&](VehicleLightType type) {
        return ReadLight(object_, type).mode == LightState::Mode::ON;
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
