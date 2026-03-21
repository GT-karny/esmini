#include "gt_esmini/control/manualdrive/NetworkPhysicsBridge.hpp"
#include "gt_esmini/control/manualdrive/ManualDriveConfig.hpp"
#include "gt_esmini/control/manualdrive/UdpTransport.hpp"
#include "gt_esmini/control/manualdrive/TcpTransport.hpp"
#include "Entities.hpp"
#include "logger.hpp"

#include <cstring>

namespace gt_esmini
{

// Same wire format as NetworkInputBridge for PedalSteerCommand
static constexpr uint32_t MAGIC_PEDAL_STEER = 0x50535443;
static constexpr size_t   PEDAL_STEER_WIRE_SIZE = 44;

NetworkPhysicsBridge::NetworkPhysicsBridge()
    : recv_buf_(65536)
{
}

NetworkPhysicsBridge::~NetworkPhysicsBridge()
{
    if (cmd_transport_)
    {
        cmd_transport_->Close();
        delete cmd_transport_;
    }
    if (state_transport_)
    {
        state_transport_->Close();
        delete state_transport_;
    }
}

bool NetworkPhysicsBridge::Init(const ManualDriveConfig& config, const scenarioengine::Object* /*obj*/)
{
    auto create_transport = [&](const std::string& type) -> ITransport* {
        if (type == "tcp") return new TcpTransport();
        return new UdpTransport();
    };

    const auto& net = config.physics_network;

    // Command transport: sends to external simulator
    cmd_transport_ = create_transport(net.transport_type);
    TransportConfig cmd_tc;
    cmd_tc.type        = net.transport_type;
    cmd_tc.host        = net.host;
    cmd_tc.target_port = net.cmd_port;
    cmd_tc.is_server   = false;  // client mode
    if (!cmd_transport_->Open(cmd_tc))
    {
        LOG_ERROR("NetworkPhysicsBridge: Failed to open command transport to {}:{}", net.host, net.cmd_port);
        return false;
    }

    // State transport: receives HVD from external simulator
    state_transport_ = create_transport(net.transport_type);
    TransportConfig state_tc;
    state_tc.type        = net.transport_type;
    state_tc.listen_port = net.state_port;
    state_tc.is_server   = true;  // server mode
    if (!state_transport_->Open(state_tc))
    {
        LOG_ERROR("NetworkPhysicsBridge: Failed to open state transport on port {}", net.state_port);
        return false;
    }

    LOG_INFO("NetworkPhysicsBridge: cmd→{}:{}, state←port {}", net.host, net.cmd_port, net.state_port);
    return true;
}

osi3::HostVehicleData NetworkPhysicsBridge::StepPedalSteer(const PedalSteerCommand& cmd, double /*dt*/)
{
    // Serialize PedalSteerCommand to wire format
    char buf[PEDAL_STEER_WIRE_SIZE];
    char* p = buf;

    uint32_t magic = MAGIC_PEDAL_STEER;
    std::memcpy(p, &magic,        4); p += 4;
    std::memcpy(p, &cmd.steering, 8); p += 8;
    std::memcpy(p, &cmd.throttle, 8); p += 8;
    std::memcpy(p, &cmd.brake,    8); p += 8;
    std::memcpy(p, &cmd.clutch,   8); p += 8;
    std::memcpy(p, &cmd.gear,     4); p += 4;
    std::memcpy(p, &cmd.buttons,  4);

    return SendAndReceive(buf, PEDAL_STEER_WIRE_SIZE);
}

#ifdef GT_ENABLE_OSI_MOTION_REQUEST
osi3::HostVehicleData NetworkPhysicsBridge::StepMotionRequest(const osi3::MotionRequest& req, double /*dt*/)
{
    // Serialize MotionRequest as protobuf
    std::string serialized = req.SerializeAsString();
    return SendAndReceive(serialized.data(), serialized.size());
}
#endif

void NetworkPhysicsBridge::SetInitialState(double /*x*/, double /*y*/, double /*z*/, double /*h*/, double /*speed*/)
{
    // External simulator manages its own initial state
}

osi3::HostVehicleData NetworkPhysicsBridge::SendAndReceive(const void* cmd_data, size_t cmd_len)
{
    // Send command
    if (cmd_transport_)
    {
        cmd_transport_->Send(cmd_data, cmd_len);
    }

    // Receive latest state (drain to get most recent)
    if (state_transport_)
    {
        int last_received = 0;
        while (true)
        {
            int r = state_transport_->Recv(recv_buf_.data(), recv_buf_.size());
            if (r <= 0) break;
            last_received = r;
        }

        if (last_received > 0)
        {
            osi3::HostVehicleData hvd;
            if (hvd.ParseFromArray(recv_buf_.data(), last_received))
            {
                last_hvd_ = hvd;
            }
        }
    }

    return last_hvd_;  // hold-last-value
}

} // namespace gt_esmini
