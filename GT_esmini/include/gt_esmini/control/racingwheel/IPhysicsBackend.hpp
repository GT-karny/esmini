#pragma once

#include "gt_esmini/control/racingwheel/RacingWheelTypes.hpp"
#include "osi_hostvehicledata.pb.h"

#ifdef GT_ENABLE_OSI_MOTION_REQUEST
#include "osi_motionrequest.pb.h"
#endif

namespace scenarioengine
{
class Object;
}

namespace gt_esmini
{

struct RacingWheelConfig;

class IPhysicsBackend
{
public:
    virtual ~IPhysicsBackend() = default;
    virtual bool Init(const RacingWheelConfig& config, const scenarioengine::Object* obj) = 0;

    virtual osi3::HostVehicleData StepPedalSteer(const PedalSteerCommand& cmd, double dt) = 0;

#ifdef GT_ENABLE_OSI_MOTION_REQUEST
    virtual osi3::HostVehicleData StepMotionRequest(const osi3::MotionRequest& req, double dt) = 0;
#endif

    virtual void SetInitialState(double x, double y, double z, double h, double speed) = 0;

    // For backends that own a vehicle model: body offset for gateway sync
    virtual void GetBodyPositionOffset(double& dx, double& dy, double& dz) const
    {
        dx = dy = dz = 0.0;
    }

    // For backends that compute dynamic attitude
    virtual void GetCombinedAttitude(double& pitch, double& roll) const
    {
        pitch = roll = 0.0;
    }
};

} // namespace gt_esmini
