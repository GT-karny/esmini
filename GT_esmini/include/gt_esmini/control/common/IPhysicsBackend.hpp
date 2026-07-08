#pragma once

#include "gt_esmini/control/common/VehicleCommand.hpp"
#include "gt_esmini/control/common/PhysicsInitParams.hpp"
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

class IPhysicsBackend
{
public:
    virtual ~IPhysicsBackend() = default;
    virtual bool Init(const PhysicsInitParams& params, const scenarioengine::Object* obj) = 0;

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

    // Live ego pose held by the backend's vehicle model (its own reference point,
    // i.e. the same origin that StepPedalSteer reports through HVD location).
    // Returns true if the backend owns a model and filled the values; backends
    // that don't (e.g. a network bridge) return false and the caller falls back
    // to the scenario object pose. Used by the driver model so its cross-track
    // feedback is closed on the *physical* ego rather than the scenario-intended
    // pose that an active lateral action writes into object->pos_ each frame.
    virtual bool GetPose(double& x, double& y, double& z, double& h, double& speed) const
    {
        (void)x; (void)y; (void)z; (void)h; (void)speed;
        return false;
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
