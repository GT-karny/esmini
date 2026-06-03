#pragma once

#include "Controller.hpp"
#include "gt_esmini/control/manualdrive/ManualDriveConfig.hpp"
#include "gt_esmini/control/manualdrive/ManualDriveTypes.hpp"
#include "gt_esmini/control/manualdrive/OverrideManager.hpp"
#include "gt_esmini/control/manualdrive/HVDStateApplier.hpp"
#include "gt_esmini/control/virtualdriver/VirtualDriverConfig.hpp"
#include "gt_esmini/control/virtualdriver/VirtualDriverTypes.hpp"
#include "osi_hostvehicledata.pb.h"

#include <vector>

#define CONTROLLER_VIRTUAL_DRIVER_TYPE_NAME "VirtualDriverController"

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
    void GetADASStates(std::vector<int>& /*states*/) const {}

    // Aggregate telemetry for GT_GetVirtualDriverTelemetry().
    const VirtualDriverTelemetry& GetTelemetry() const { return telemetry_; }

private:
    int    BuildLightMaskFromExtension() const;
    int    DetectManeuverDir(const ShortPlannerSnapshot& plan) const;
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

    OverrideManager  override_mgr_;
    HVDStateApplier  state_applier_;

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

    // Active SpeedAction dynamics tracking (Option C): reconstruct the commanded
    // speed profile from the action's TransitionDynamics. esmini's SpeedAction::Start
    // (which runs under MODE_ADDITIVE) sets start/target speed and normalizes the
    // duration to the time domain (distance/rate are pre-converted), so we evaluate
    // the shape against our own elapsed time — no dependency on the transition's
    // per-frame progression under controller ownership.
    const void* speed_action_id_   = nullptr;
    double      speed_start_t_      = 0.0;
    double      last_action_target_ = 0.0;  // target of the most recent SpeedAction; held when idle
};

scenarioengine::Controller* InstantiateControllerVirtualDriver(void* args);

}  // namespace gt_esmini
