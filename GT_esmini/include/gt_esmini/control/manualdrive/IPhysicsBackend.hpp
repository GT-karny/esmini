#pragma once

#include "gt_esmini/control/manualdrive/ManualDriveTypes.hpp"
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

struct ManualDriveConfig;

class IPhysicsBackend
{
public:
    virtual ~IPhysicsBackend() = default;
    virtual bool Init(const ManualDriveConfig& config, const scenarioengine::Object* obj) = 0;

    virtual osi3::HostVehicleData StepPedalSteer(const PedalSteerCommand& cmd, double dt) = 0;

#ifdef GT_ENABLE_OSI_MOTION_REQUEST
    virtual osi3::HostVehicleData StepMotionRequest(const osi3::MotionRequest& req, double dt) = 0;
#endif

    virtual void SetInitialState(double x, double y, double z, double h, double speed) = 0;

    // Re-synchronize position/heading from scenario engine (e.g. on AUTO→MANUAL transition).
    // Unlike SetInitialState, this preserves dynamic state (gear, RPM, etc.).
    virtual void SyncState(double x, double y, double z, double h, double speed)
    {
        SetInitialState(x, y, z, h, speed);
    }

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

    // Feed road Z back for correct HVD export. Called after Apply().
    virtual void SyncRoadZ(double road_z) { (void)road_z; }

    // Dynamic-only attitude (spring-damper), excluding terrain component.
    // Gateway uses P_REL/R_REL which adds road pitch/roll automatically.
    virtual void GetDynamicAttitude(double& pitch, double& roll) const
    {
        pitch = roll = 0.0;
    }
};

} // namespace gt_esmini
