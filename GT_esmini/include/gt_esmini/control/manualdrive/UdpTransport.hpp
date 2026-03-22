#pragma once

#include "gt_esmini/control/manualdrive/ITransport.hpp"

class UDPServer;

namespace gt_esmini
{

class GT_UDP_Sender;

class UdpTransport : public ITransport
{
public:
    UdpTransport();
    ~UdpTransport();

    bool Open(const TransportConfig& config) override;
    int  Send(const void* data, size_t len) override;
    int  Recv(void* buf, size_t max_len) override;
    void Close() override;

private:
    UDPServer*     server_ = nullptr;  // for receiving
    GT_UDP_Sender* sender_ = nullptr;  // for sending
};

} // namespace gt_esmini
