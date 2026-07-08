#pragma once

#include "Controller.hpp"
#include "gt_esmini/control/manualdrive/ManualDriveConfig.hpp"
#include "gt_esmini/control/manualdrive/ManualDriveTypes.hpp"
#include "gt_esmini/control/manualdrive/OverrideManager.hpp"
#include "gt_esmini/control/manualdrive/HVDStateApplier.hpp"
#include "osi_hostvehicledata.pb.h"
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

    const char* GetTypeName() const override { return CONTROLLER_MANUAL_DRIVE_TYPE_NAME; }

    // OSI getters (called by GT_Step for HVD reporting)
    void GetInputsForOSI(double& throttle, double& brake, double& steering, int& gear, int& lightMask) const;
    void GetPowertrainForOSI(double& rpm, double& torque) const;
    void GetADASStates(std::vector<int>& /*states*/) const {}  // no ADAS in ManualDrive

private:
    friend class ManualDriveCoordinator;

    int BuildLightMaskFromExtension() const;

    ManualDriveConfig       config_;
    IInputSource*           input_source_;
    IPhysicsBackend*        physics_backend_;
    IFFBSink*               ffb_sink_;
    OverrideManager         override_mgr_;
    HVDStateApplier         state_applier_;
    ManualDriveCoordinator* coordinator_;

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
};

scenarioengine::Controller* InstantiateControllerManualDrive(void* args);

} // namespace gt_esmini
