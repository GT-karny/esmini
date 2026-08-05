#pragma once

#include "Controller.hpp"
#include "gt_esmini/control/manualdrive/ManualDriveConfig.hpp"
#include "gt_esmini/control/manualdrive/ManualDriveTypes.hpp"
#include "gt_esmini/control/manualdrive/OverrideManager.hpp"
#include "gt_esmini/control/manualdrive/HVDStateApplier.hpp"
// req-vd-ad:REQ-AD-025/028, vd-func:FUNC-075 -- phase A ManualDrive ADAS
// coexistence stack (manualdrive_adas_design.md §2-1/§10). Pulled in fully
// (not forward-declared) because ManualAdasFrameResult is held BY VALUE below
// (adas_last_result_) and AdasCoexistenceStack itself by unique_ptr.
#include "gt_esmini/control/manualdrive/AdasCoexistenceStack.hpp"
#include "osi_hostvehicledata.pb.h"
#include <memory>
#include <vector>

#define CONTROLLER_MANUAL_DRIVE_TYPE_NAME "ManualDriveController"

namespace gt_esmini
{

class IInputSource;
class IPhysicsBackend;
class IFFBSink;
class ManualDriveCoordinator;

class ControllerManualDrive : public scenarioengine::Controller
{
public:
    ControllerManualDrive(InitArgs* args);
    ~ControllerManualDrive() override;

    void Step(double timeStep) override;
    int  Activate(const ControlActivationMode (&mode)[static_cast<unsigned int>(ControlDomains::COUNT)]) override;
    void Deactivate() override;
    // feature:F7 — overridden solely to keep the GT ownership ledger in step with
    // the base class's domain bitmask. From OSC v1.3 a domain can be taken away
    // through this call without Deactivate() ever running, so a ledger updated
    // only from Activate()/Deactivate() would keep asserting ownership of a
    // domain this controller no longer holds.
    void DeactivateDomains(unsigned int domains) override;

    const char* GetTypeName() const override { return CONTROLLER_MANUAL_DRIVE_TYPE_NAME; }

    // OSI getters (called by GT_Step for HVD reporting)
    void GetInputsForOSI(double& throttle, double& brake, double& steering, int& gear, int& lightMask) const;
    void GetPowertrainForOSI(double& rpm, double& torque) const;

    // Deliberately EMPTY, always. This is load-bearing, not an oversight: the
    // ManualDrive report path (GetADASFunctions() below) reports through
    // GT_esminiLib.cpp's AddADASFunctionEx loop instead, and that file's
    // pushControllerState lambda only emits the fixed 24-slot RealDriver rows
    // when `adasStates.size() >= gt_esmini::realdetail::kAdasFunctionCount`
    // (GT_esminiLib.cpp, the ManualDrive dispatch branch). An empty vector here
    // is exactly what keeps ManualDrive OUT of that legacy 24-slot path -- if
    // this ever returned 24 (or more) entries, GT_Step would emit those AS
    // WELL AS the 2 real gt.aeb/gt.fcw rows from GetADASFunctions(), corrupting
    // the OSI stream with spurious duplicate/placeholder rows alongside the
    // real ones. test_hvd_dispatch_invariance.py (scripts/verification/) pins
    // both halves of this: RealDriver's 24-row block is unaffected by this
    // wiring, and ManualDrive's own block never grows the 24-slot shape.
    void GetADASStates(std::vector<int>& /*states*/) const {}

    // req-vd-ad:REQ-AD-025/028, vd-func:FUNC-075 (design §8-1) -- mirrors
    // ControllerVirtualDriver::GetADASFunctions()'s shape. Called by GT_Step
    // AFTER this controller's Step() has already run for the frame, so it
    // reports the ManualAdasFrameResult cached in adas_last_result_ / the
    // ownership flag cached in adas_last_owns_longitudinal_ -- i.e. THIS
    // frame's AdasCoexistenceStack::Step() decision, not last frame's. That is
    // the correct frame to report: ManualDriveCoordinator::RunFrame runs the
    // stack once per Step() call and caches its result immediately afterward
    // (same call), so by the time GT_Step reaches this getter the cached
    // result already reflects the frame that was just stepped -- reporting
    // anything older would make the OSI stream's AEB state lag one frame
    // behind the brake_out value HVD's own vehicle_brake_system field carries
    // for the SAME frame, which is exactly the kind of instrument/reality
    // mismatch this project treats as a defect (see MEMORY's
    // verification_instrument_fidelity note).
    void GetADASFunctions(std::vector<AdasFunctionState>& functions) const;

private:
    friend class ManualDriveCoordinator;

    int BuildLightMaskFromExtension() const;
    bool ResumeVirtualDriverControl();
    void ReleaseFfbOutputs();

    ManualDriveConfig       config_;
    IInputSource*           input_source_;
    IPhysicsBackend*        physics_backend_;
    IFFBSink*               ffb_sink_;
    OverrideManager         override_mgr_;
    HVDStateApplier         state_applier_;
    ManualDriveCoordinator* coordinator_;

    // req-vd-ad:REQ-AD-025/028, vd-func:FUNC-075 (design §2-1/§10, phase A).
    // unique_ptr (not by-value) because its ManualAdasStackConfig depends on
    // config_, which is only fully loaded partway through the constructor
    // BODY (LoadFromFile() -- not a default member initializer), i.e. after
    // this member would already have been default-constructed if held by
    // value; a unique_ptr lets construction happen once, in the body, with
    // the real config -- same reasoning as input_source_/physics_backend_
    // below being raw-pointer-`new`'d in the body rather than default members.
    std::unique_ptr<AdasCoexistenceStack> adas_stack_;

    // This frame's AdasCoexistenceStack::Step() output, cached by
    // ManualDriveCoordinator::RunFrame for GetADASFunctions() (see that
    // method's comment for why "this frame", not last frame, is correct).
    // adas_last_owns_longitudinal_ is cached alongside it because
    // BuildManualAdasFunctionReport() takes domain ownership as a SEPARATE
    // bool argument (not part of ManualAdasFrameResult) -- see design §2-3.
    ManualAdasFrameResult adas_last_result_;
    bool                  adas_last_owns_longitudinal_ = false;
    // req-vd-ad:REQ-AD-027 (phase D): the LATERAL half of the same cache. A
    // SECOND flag, not a reuse of the one above -- a split configuration is
    // exactly the case where the two differ, and that case is what
    // md-split-no-double-equipment is about (design §2-3).
    bool                  adas_last_owns_lateral_      = false;

    // TrafficPolicyContext::sim_time source (design §2-1). NOT an upstream
    // scenarioengine::Controller base-class member -- see
    // ManualDriveCoordinator::RunFrame's comment at its accumulation site for
    // why ManualDrive keeps its own, mirroring ControllerVirtualDriver's own
    // sim_time_ member (ControllerVirtualDriver.hpp) rather than inheriting one.
    double sim_time_ = 0.0;

    osi3::HostVehicleData   current_hvd_;
    PedalSteerCommand       last_cmd_;

    // Light toggle states (flip on button rising edge)
    bool         headlight_on_ = false;
    bool         high_beam_on_ = false;
    bool         fog_light_on_ = false;
    bool         hazard_on_    = false;

    // Indicator auto-cancel
    IndicatorFSM indicator_fsm_;
    uint32_t     prev_buttons_  = 0;
    double       prev_steering_ = 0.0;

    // Accumulated sim time for the GT light blink ticker (R5-U3).
    double       light_sim_clock_ = 0.0;

    // feature:F7 S2 — true while this controller is the object's designated
    // physics integrator (DomainOwnershipLedger::IsIntegrator). Only the
    // integrator advances the body; see the ledger header for why exactly one
    // is picked and why it is picked there rather than from Step order. Kept as
    // state so the backend can be re-synced when integration is taken over,
    // since it stood still while another controller moved the car.
    bool         was_domain_integrator_ = false;
    bool         input_source_initialized_ = false;
};

scenarioengine::Controller* InstantiateControllerManualDrive(void* args);

} // namespace gt_esmini
