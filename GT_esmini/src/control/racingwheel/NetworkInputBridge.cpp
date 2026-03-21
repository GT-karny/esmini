#include "gt_esmini/control/racingwheel/NetworkInputBridge.hpp"
#include "gt_esmini/control/racingwheel/RacingWheelConfig.hpp"
#include "gt_esmini/control/racingwheel/UdpTransport.hpp"
#include "gt_esmini/control/racingwheel/TcpTransport.hpp"
#include "logger.hpp"

#include <cstring>

namespace gt_esmini
{

// Wire format for PedalSteerCommand:
// [4B magic][8B steering][8B throttle][8B brake][8B clutch][4B gear][4B buttons] = 44 bytes
static constexpr size_t PEDAL_STEER_WIRE_SIZE = 44;

NetworkInputBridge::NetworkInputBridge()
    : recv_buf_(4096)
{
}

NetworkInputBridge::~NetworkInputBridge()
{
    Shutdown();
}

bool NetworkInputBridge::Init(const RacingWheelConfig& config)
{
    level_ = config.input_network.level;

    // Create transport
    if (config.input_network.transport_type == "tcp")
    {
        transport_ = new TcpTransport();
    }
    else
    {
        transport_ = new UdpTransport();
    }

    TransportConfig tc;
    tc.type        = config.input_network.transport_type;
    tc.listen_port = config.input_network.port;
    tc.is_server   = true;

    if (!transport_->Open(tc))
    {
        LOG_ERROR("NetworkInputBridge: Failed to open transport on port {}", config.input_network.port);
        return false;
    }

    LOG_INFO("NetworkInputBridge: Listening on port {} (level={}, transport={})",
             config.input_network.port, level_, config.input_network.transport_type);
    return true;
}

InputFrame NetworkInputBridge::Poll(double /*dt*/)
{
    InputFrame frame;

    if (!transport_)
    {
        return frame;
    }

    // Drain all pending packets, keep latest
    bool got_new = false;
    while (true)
    {
        int received = transport_->Recv(recv_buf_.data(), recv_buf_.size());
        if (received <= 0)
        {
            break;
        }

        if (level_ == "pedal_steer" && static_cast<size_t>(received) >= PEDAL_STEER_WIRE_SIZE)
        {
            const char* p = recv_buf_.data();

            // Check magic
            uint32_t magic = 0;
            std::memcpy(&magic, p, 4);
            if (magic != MAGIC_PEDAL_STEER)
            {
                continue;  // Skip invalid packet
            }
            p += 4;

            std::memcpy(&last_cmd_.steering, p, 8); p += 8;
            std::memcpy(&last_cmd_.throttle, p, 8); p += 8;
            std::memcpy(&last_cmd_.brake,    p, 8); p += 8;
            std::memcpy(&last_cmd_.clutch,   p, 8); p += 8;
            std::memcpy(&last_cmd_.gear,     p, 4); p += 4;
            std::memcpy(&last_cmd_.buttons,  p, 4);

            got_new = true;
            has_data_ = true;
        }
#ifdef GT_ENABLE_OSI_MOTION_REQUEST
        else if (level_ == "motion_request" && received > 0)
        {
            osi3::MotionRequest req;
            if (req.ParseFromArray(recv_buf_.data(), received))
            {
                frame.motion_request = req;
                frame.connected = true;
                has_data_ = true;
                got_new = true;
            }
        }
#endif
    }

    // Return last known command (hold-last-value)
    if (has_data_ && level_ == "pedal_steer")
    {
        frame.pedal_steer = last_cmd_;
        frame.connected = true;
    }

    return frame;
}

void NetworkInputBridge::Shutdown()
{
    if (transport_)
    {
        transport_->Close();
        delete transport_;
        transport_ = nullptr;
    }
}

bool NetworkInputBridge::IsConnected() const
{
    return has_data_;
}

} // namespace gt_esmini
