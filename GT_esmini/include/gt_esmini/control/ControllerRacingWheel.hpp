#pragma once

#include "Controller.hpp"
#include "gt_esmini/control/racingwheel/RacingWheelConfig.hpp"
#include "gt_esmini/control/racingwheel/RacingWheelTypes.hpp"
#include "gt_esmini/control/racingwheel/OverrideManager.hpp"
#include "gt_esmini/control/racingwheel/HVDStateApplier.hpp"
#include "osi_hostvehicledata.pb.h"

#define CONTROLLER_RACING_WHEEL_TYPE_NAME "RacingWheelController"

namespace gt_esmini
{

class IInputSource;
class IPhysicsBackend;
class IFFBSink;
class RacingWheelCoordinator;

class ControllerRacingWheel : public scenarioengine::Controller
{
public:
    ControllerRacingWheel(InitArgs* args);
    ~ControllerRacingWheel() override;

    void Step(double timeStep) override;
    int  Activate(const ControlActivationMode (&mode)[static_cast<unsigned int>(ControlDomains::COUNT)]) override;

    const char* GetTypeName() override { return CONTROLLER_RACING_WHEEL_TYPE_NAME; }

    // OSI getters (called by GT_Step for HVD reporting)
    void GetInputsForOSI(double& throttle, double& brake, double& steering, int& gear) const;
    void GetPowertrainForOSI(double& rpm, double& torque) const;

private:
    friend class RacingWheelCoordinator;

    RacingWheelConfig       config_;
    IInputSource*           input_source_;
    IPhysicsBackend*        physics_backend_;
    IFFBSink*               ffb_sink_;
    OverrideManager         override_mgr_;
    HVDStateApplier         state_applier_;
    RacingWheelCoordinator* coordinator_;

    osi3::HostVehicleData   current_hvd_;
    PedalSteerCommand       last_cmd_;
};

scenarioengine::Controller* InstantiateControllerRacingWheel(void* args);

} // namespace gt_esmini
