#pragma once

#include "Controller.hpp"
#include "gt_esmini/control/manualdrive/ManualDriveConfig.hpp"
#include "gt_esmini/control/manualdrive/ManualDriveTypes.hpp"
#include "gt_esmini/control/manualdrive/OverrideManager.hpp"
#include "gt_esmini/control/manualdrive/HVDStateApplier.hpp"
#include "osi_hostvehicledata.pb.h"

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

    const char* GetTypeName() override { return CONTROLLER_MANUAL_DRIVE_TYPE_NAME; }

    // OSI getters (called by GT_Step for HVD reporting)
    void GetInputsForOSI(double& throttle, double& brake, double& steering, int& gear) const;
    void GetPowertrainForOSI(double& rpm, double& torque) const;

private:
    friend class ManualDriveCoordinator;

    ManualDriveConfig       config_;
    IInputSource*           input_source_;
    IPhysicsBackend*        physics_backend_;
    IFFBSink*               ffb_sink_;
    OverrideManager         override_mgr_;
    HVDStateApplier         state_applier_;
    ManualDriveCoordinator* coordinator_;

    osi3::HostVehicleData   current_hvd_;
    PedalSteerCommand       last_cmd_;
};

scenarioengine::Controller* InstantiateControllerManualDrive(void* args);

} // namespace gt_esmini
