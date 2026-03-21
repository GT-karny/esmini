#include "gt_esmini/control/manualdrive/UdpTransport.hpp"
#include "gt_esmini/io/GT_UDP.hpp"
#include "UDP.hpp"
#include "logger.hpp"

namespace gt_esmini
{

UdpTransport::UdpTransport() = default;

UdpTransport::~UdpTransport()
{
    Close();
}

bool UdpTransport::Open(const TransportConfig& config)
{
    // Create receiver if listen_port is set
    if (config.listen_port > 0)
    {
        server_ = new UDPServer(static_cast<unsigned short>(config.listen_port), 1);  // 1ms timeout = non-blocking
        LOG_INFO("UdpTransport: Listening on port {}", config.listen_port);
    }

    // Create sender if target_port is set
    if (config.target_port > 0)
    {
        sender_ = new GT_UDP_Sender(static_cast<unsigned short>(config.target_port), config.host);
        LOG_INFO("UdpTransport: Sending to {}:{}", config.host, config.target_port);
    }

    return true;
}

int UdpTransport::Send(const void* data, size_t len)
{
    if (!sender_)
    {
        return -1;
    }
    return sender_->Send(static_cast<const char*>(data), static_cast<unsigned int>(len));
}

int UdpTransport::Recv(void* buf, size_t max_len)
{
    if (!server_)
    {
        return 0;
    }
    int received = server_->Receive(static_cast<char*>(buf), static_cast<unsigned int>(max_len));
    return (received > 0) ? received : 0;
}

void UdpTransport::Close()
{
    delete server_;
    server_ = nullptr;
    delete sender_;
    sender_ = nullptr;
}

} // namespace gt_esmini
