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
#include "osi_hostvehicledata.pb.h"

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
    // Target speed the driver tracks. Read from a running SpeedAction (which the
    // engine no longer applies to object speed once a controller owns the LONG
    // domain) and latched so it persists after the action completes.
    double ResolveTargetSpeed();

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
};

scenarioengine::Controller* InstantiateControllerVirtualDriver(void* args);

}  // namespace gt_esmini
