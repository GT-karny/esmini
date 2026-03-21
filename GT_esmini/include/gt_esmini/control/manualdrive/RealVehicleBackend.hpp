#pragma once

#include "gt_esmini/control/manualdrive/IPhysicsBackend.hpp"
#include "gt_esmini/control/RealVehicle.hpp"

namespace gt_esmini
{

class RealVehicleBackend : public IPhysicsBackend
{
public:
    bool Init(const ManualDriveConfig& config, const scenarioengine::Object* obj) override;

    osi3::HostVehicleData StepPedalSteer(const PedalSteerCommand& cmd, double dt) override;

#ifdef GT_ENABLE_OSI_MOTION_REQUEST
    osi3::HostVehicleData StepMotionRequest(const osi3::MotionRequest& req, double dt) override;
#endif

    void SetInitialState(double x, double y, double z, double h, double speed) override;
    void GetBodyPositionOffset(double& dx, double& dy, double& dz) const override;
    void GetCombinedAttitude(double& pitch, double& roll) const override;

    RealVehicle& GetRealVehicle() { return real_vehicle_; }

private:
    osi3::HostVehicleData BuildHVD(const PedalSteerCommand& cmd) const;

    RealVehicle real_vehicle_;
    PedalSteerCommand last_cmd_;
};

} // namespace gt_esmini
