#pragma once

#include "gt_esmini/control/manualdrive/IPhysicsBackend.hpp"
#include "gt_esmini/control/manualdrive/ITransport.hpp"

#include <vector>

namespace gt_esmini
{

class NetworkPhysicsBridge : public IPhysicsBackend
{
public:
    NetworkPhysicsBridge();
    ~NetworkPhysicsBridge();

    bool Init(const ManualDriveConfig& config, const scenarioengine::Object* obj) override;

    osi3::HostVehicleData StepPedalSteer(const PedalSteerCommand& cmd, double dt) override;

#ifdef GT_ENABLE_OSI_MOTION_REQUEST
    osi3::HostVehicleData StepMotionRequest(const osi3::MotionRequest& req, double dt) override;
#endif

    void SetInitialState(double x, double y, double z, double h, double speed) override;

private:
    osi3::HostVehicleData SendAndReceive(const void* cmd_data, size_t cmd_len);

    ITransport* cmd_transport_   = nullptr;  // send commands
    ITransport* state_transport_ = nullptr;  // receive state

    osi3::HostVehicleData last_hvd_;  // hold-last-value
    std::vector<char>     recv_buf_;
};

} // namespace gt_esmini
