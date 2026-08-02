#pragma once

#include "Controller.hpp"
#include "gt_esmini/control/manualdrive/ManualDriveConfig.hpp"
#include "gt_esmini/control/manualdrive/ManualDriveTypes.hpp"
#include "gt_esmini/control/manualdrive/OverrideManager.hpp"
#include "gt_esmini/control/manualdrive/HVDStateApplier.hpp"
#include "gt_esmini/control/virtualdriver/VirtualDriverConfig.hpp"
#include "gt_esmini/control/virtualdriver/VirtualDriverTypes.hpp"
#include "gt_esmini/control/virtualdriver/AdasFunctionReport.hpp"
#include "gt_esmini/control/virtualdriver/AdSteeringEnvelope.hpp"
#include "gt_esmini/control/virtualdriver/ResumeMergeProfile.hpp"
#include "gt_esmini/control/virtualdriver/RouteLanePlan.hpp"
#include "gt_esmini/control/virtualdriver/LaneChangeInitiation.hpp"
#include "osi_hostvehicledata.pb.h"

#include <cstddef>
#include <string>
#include <vector>

#define CONTROLLER_VIRTUAL_DRIVER_TYPE_NAME "VirtualDriverController"

namespace scenarioengine
{
class LatLaneChangeAction;
}

namespace gt_esmini
{

// User-range controller type id (USER_CONTROLLER_TYPE_BASE = 1000).
// RouteDrive = 1001, Kinematic uses CONTROLLER_TYPE_KINEMATIC; VirtualDriver = 1002.
constexpr int CONTROLLER_TYPE_VIRTUAL_DRIVER = 1002;

class IInputSource;
class IPhysicsBackend;
class IShortPlanner;
class IMidLongPlanner;
class IDriverModel;
class IIndicatorPolicy;
class TrafficPolicyManager;

// ControllerVirtualDriver — a full-physics "virtual driver".
//
// Reproduces Default-controller behavior (route following, SpeedAction,
// LaneChangeAction) by driving a full vehicle model (RealVehicleBackend) with
// internally generated pedal/steer commands. Runs MODE_ADDITIVE: each frame the
// storyboard sets the targets (speed / lane change), the short planner builds an
// equal-Δt trajectory preview, the driver model (PID + pure pursuit) inverts it
// into pedal/steer, physics integrates, and the result is written back to the
// object (physics owns pos_). Manual override (via a reused IInputSource +
// OverrideManager) can take either domain at any time.
class ControllerVirtualDriver : public scenarioengine::Controller
{
public:
    ControllerVirtualDriver(InitArgs* args);
    ~ControllerVirtualDriver() override;

    void Init() override;
    void Step(double timeStep) override;
    int  Activate(const ControlActivationMode (&mode)[static_cast<unsigned int>(ControlDomains::COUNT)]) override;
    void Deactivate() override;
    // feature:F7 — MUST stay overridden. From OSC v1.3 an ActivateControllerAction
    // that hands a domain to another controller deactivates the incumbent through
    // this call and never touches Deactivate(); leaving it to the base class lets
    // the FFB servo keep pulling a wheel this controller no longer steers.
    void DeactivateDomains(unsigned int domains) override;

    const char* GetTypeName() const override { return CONTROLLER_VIRTUAL_DRIVER_TYPE_NAME; }
    scenarioengine::Controller::Type GetType() const override
    {
        return static_cast<scenarioengine::Controller::Type>(CONTROLLER_TYPE_VIRTUAL_DRIVER);
    }

    // OSI getters (called by GT_Step for HVD reporting) — same contract as ManualDrive.
    void GetInputsForOSI(double& throttle, double& brake, double& steering, int& gear, int& lightMask) const;
    void GetPowertrainForOSI(double& rpm, double& torque) const;
    // Legacy fixed-24-slot label path (ControllerRealDriver / PythonDriver use
    // it). VirtualDriver deliberately reports nothing here — its functions do
    // not line up with that fixed array — and uses GetADASFunctions() instead.
    void GetADASStates(std::vector<int>& /*states*/) const {}

    // W1: the VD stack's automated-driving functions for this frame, as OSI
    // HostVehicleData.vehicle_automated_driving_function[] rows (name/state/
    // custom_name/custom_detail). This is the ONLY path by which face3 can see
    // that e.g. AEB engaged, per the §0.2 contract that face2 is observed
    // through face1's OSI rather than through a direct telemetry tap.
    void GetADASFunctions(std::vector<AdasFunctionState>& functions) const;

    // Aggregate telemetry for GT_GetVirtualDriverTelemetry().
    const VirtualDriverTelemetry& GetTelemetry() const { return telemetry_; }

private:
    int    BuildLightMaskFromExtension() const;
    // Turn-indicator direction of an in-progress lane change; +1 left, -1 right, 0 none.
    // Derived from the LatLaneChangeAction's target lane (NOT from preview-point geometry, whose
    // body-frame sign inverts once the ego's own yaw builds up) and latched for the action's life.
    int    DetectManeuverDir();
    int    ResolveLaneChangeDir(const scenarioengine::LatLaneChangeAction* lc) const;
    // Look ahead along the route for a junction turn; +1 left, -1 right, 0 none.
    // Used to pre-arm turn signals before intersections (no lane change involved).
    int    DetectJunctionTurn(double speed) const;
    void   ApplyLights(const PedalSteerCommand& cmd, const IndicatorSnapshot& ind);
    // feature:F7 resume-merge -- resolve the ego's ROUTE lane at its current
    // track/s (design doc resume_merge_trajectory_design.md section 2-0-1).
    // Returns "" (success; out_track/out_lane valid) or a short fallback
    // reason ("no_route" | "off_route" | "track_mismatch") mirrored into
    // telemetry's resume_merge.fallback_reason. Uses an ISOLATED route clone
    // internally (pos.CopyRoute, same pattern as JunctionTurn.hpp), so it
    // never mutates object_->pos_'s shared Route*.
    const char* ResolveResumeMergeRouteLane(unsigned int& out_track, int& out_lane) const;
    // Target speed the driver tracks. Read from a running SpeedAction (which the
    // engine no longer applies to object speed once a controller owns the LONG
    // domain) and latched so it persists after the action completes.
    double ResolveTargetSpeed();

    // feature:F7 scenario-driven handover -- paired setup/teardown for everything
    // this controller drives outside itself (physics backend, input source, force
    // feedback, intervention latch). Called from Activate() on the inactive<->active
    // transition; teardown is also reached from Deactivate(). See design doc
    // scenario_control_handoff_design.md.
    void SetUpControlOutputs();
    void TearDownControlOutputs();

    // feature:F7 S2 — true while this controller is the object's designated
    // physics integrator (DomainOwnershipLedger::IsIntegrator). Only the
    // integrator advances the body and writes object->pos_; a non-integrator
    // that also integrated would race the real owner and hand the whole vehicle
    // to whichever controller happened to Step() last. Kept as state so the
    // transition can be handled: the backend is re-synced from the object pose
    // when this controller takes over integration, since it has been standing
    // still while someone else moved the car.
    bool was_domain_integrator_ = false;

    // feature:F7 — guards TearDownControlOutputs() against a second release.
    // Starts true: nothing has been set up yet, so there is nothing to release
    // (a Deactivate() on a controller that was never activated must be a
    // no-op, not a teardown against a null input source).
    bool control_outputs_released_ = true;

    // feature:F7 — IInputSource::Init() has no repeat-call guard. On the
    // sdl2_wheel path a second call re-opens the joystick, orphans the haptic
    // effect IDs the previous open created, and re-runs the 500 ms axis-settle
    // loop mid-frame. A scenario handover that later hands control back makes
    // that a normal occurrence, not an edge case, so the device is opened once
    // and kept alive across deactivation (matching ControllerManualDrive).
    bool input_source_initialized_ = false;

    VirtualDriverConfig vd_config_;
    ManualDriveConfig   io_config_;  // built from vd_config_ for IInputSource + OverrideManager

    IInputSource*    input_source_    = nullptr;
    IPhysicsBackend* physics_backend_ = nullptr;
    IShortPlanner*   short_planner_   = nullptr;
    IMidLongPlanner* midlong_planner_ = nullptr;
    IDriverModel*    driver_model_    = nullptr;
    IIndicatorPolicy* indicator_policy_ = nullptr;
    // Phase 3: bundles the enabled traffic policies (lead-vehicle, traffic-light,
    // stop/yield sign). Evaluated each frame; its constraints feed the mid/long
    // planner (MidLongContext::policy), which folds them into v_target(s).
    TrafficPolicyManager* traffic_policy_mgr_ = nullptr;

    OverrideManager  override_mgr_;
    HVDStateApplier  state_applier_;

    // feature:F7 — AD steering safety envelope (AdSteeringEnvelope.hpp). Config
    // is built once in the constructor (not hot-reloaded during a run);
    // ad_envelope_state_ (angle AND realized-rate anchors) is updated every
    // Step() via UpdateAdSteeringEnvelopeState() with whichever steering
    // command was ACTUALLY applied that frame (AUTO's clamped output or
    // MANUAL's raw input) — see Step() for the core design invariant.
    AdSteeringEnvelopeConfig ad_envelope_cfg_;
    AdSteeringEnvelopeState  ad_envelope_state_;

    // feature:F7 resume-merge (docs/virtualdriver/design/resume_merge_trajectory_design.md).
    // Config captured once at construction (not hot-reloaded), mirroring
    // ad_envelope_cfg_ above -- Step() gates ALL resume-merge logic behind
    // resume_merge_cfg_.enabled so the disabled path (shipped default) runs
    // no new arithmetic at all. resume_merge_state_ persists the armed
    // hand-over capture (d0/v0_lat/a0_lat/T) across frames. prev_heading_ is
    // the rolling one-frame-back ego heading used to derive
    // a0_lat = yaw_rate * speed at the instant of arming (design doc section
    // 8-3(a)); only tracked while resume_merge_cfg_.enabled.
    ResumeMergeConfig resume_merge_cfg_;
    ResumeMergeState   resume_merge_state_{};
    double             prev_heading_       = 0.0;
    bool               prev_heading_valid_ = false;

    // vd-func:FUNC-055 AD lane-change initiation
    // (docs/virtualdriver/design/lane_change_initiation.md). lc_init_cfg_/lc_init_state_ are this
    // layer's OWN decision state (which hop, if any, is in progress); lc_merge_cfg_/lc_merge_state_
    // are a SEPARATE ResumeMergeProfile instance driving that hop's trajectory -- deliberately NOT
    // sharing storage with resume_merge_cfg_/resume_merge_state_ above (design doc section 8 tail).
    // lc_prev_heading_/lc_prev_heading_valid_ mirror prev_heading_/prev_heading_valid_ above but are
    // updated independently, gated only on lc_init_cfg_.enabled: sharing the resume-merge pair would
    // silently starve a0_lat capture whenever resume_merge_enabled is false but this feature is on.
    LaneChangeInitiationConfig lc_init_cfg_;
    LaneChangeInitiationState  lc_init_state_{};
    ResumeMergeConfig          lc_merge_cfg_;
    ResumeMergeState           lc_merge_state_{};
    double                     lc_prev_heading_       = 0.0;
    bool                       lc_prev_heading_valid_ = false;

    // Manual indicator (turn-signal) control via input-source buttons, reusing
    // ManualDrive's auto-cancel FSM. When the human arms an indicator it takes
    // precedence over the auto (maneuver-driven) policy.
    IndicatorFSM     indicator_fsm_;
    uint32_t         prev_buttons_  = 0;
    double           prev_steering_ = 0.0;

    osi3::HostVehicleData  current_hvd_;
    PedalSteerCommand      last_cmd_;
    VirtualDriverTelemetry telemetry_;

    double sim_time_          = 0.0;
    double last_target_speed_ = 0.0;  // current speed reference (held between actions)
    bool   target_initialized_ = false;

    // Brake-light debounce: the speed PID emits brake micro-pulses while tracking
    // the stepped speed-planner reference, which would flicker the brake light. The
    // light latches ON on any brake and stays on for a short hold after the last
    // pulse, bridging the gaps between pulses.
    bool   brake_light_on_         = false;
    double brake_light_hold_until_ = 0.0;

    // Active SpeedAction dynamics tracking (Option C): reconstruct the commanded
    // speed profile from the action's TransitionDynamics. esmini's SpeedAction::Start
    // (which runs under MODE_ADDITIVE) sets start/target speed and normalizes the
    // duration to the time domain (distance/rate are pre-converted), so we evaluate
    // the shape against our own elapsed time — no dependency on the transition's
    // per-frame progression under controller ownership.
    const void* speed_action_id_   = nullptr;
    double      speed_start_t_      = 0.0;
    double      last_action_target_ = 0.0;  // target of the most recent SpeedAction; held when idle

    // Lane-change indicator latch: direction is resolved once, when the action starts, and held for
    // that action's lifetime (see DetectManeuverDir).
    const void* lane_change_action_id_ = nullptr;
    int         lane_change_dir_       = 0;

    // RouteLanePlan (control/virtualdriver/RouteLanePlan.hpp) -- per-frame "is the
    // ego on a lane that still leads to the route's destination" diagnostic. See
    // Step() for the evaluation and telemetry_.route_lane for the published
    // snapshot; route_lane_plan_ itself is rebuilt only when the route changes
    // (route_lane_cache_route_/route_lane_cache_hash_ below), not every frame.
    RouteLanePlan route_lane_plan_;
    const void*   route_lane_cache_route_ = nullptr;  // Route* identity the plan was built from
    size_t        route_lane_cache_hash_  = 0;        // hash of the route's (track,lane) waypoint skeleton
    bool          route_lane_warned_      = false;    // whether the CURRENT diagnostic has been logged
    std::string   route_lane_warned_reason_;          // the diagnostic value that was last logged

    // Previous frame's road/lane match, kept for the deviation check (did the ego
    // just leave a road it was off its target lane(s) on) and its log message.
    id_t             route_lane_prev_road_ = ID_UNDEFINED;
    int              route_lane_prev_lane_ = 0;
    std::vector<int> route_lane_prev_target_lanes_;
    bool             route_lane_prev_on_target_ = true;
    int              route_lane_deviations_      = 0;
    id_t             route_lane_last_dev_road_   = ID_UNDEFINED;
};

scenarioengine::Controller* InstantiateControllerVirtualDriver(void* args);

}  // namespace gt_esmini
