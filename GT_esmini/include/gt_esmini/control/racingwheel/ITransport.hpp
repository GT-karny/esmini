#pragma once

#include <cstddef>
#include <string>

namespace gt_esmini
{

struct TransportConfig
{
    std::string type       = "udp";        // "udp" or "tcp"
    std::string host       = "127.0.0.1";
    int         listen_port  = 0;
    int         target_port  = 0;
    bool        is_server    = true;       // tcp: server or client mode
};

class ITransport
{
public:
    virtual ~ITransport() = default;
    virtual bool Open(const TransportConfig& config) = 0;
    virtual int  Send(const void* data, size_t len) = 0;
    virtual int  Recv(void* buf, size_t max_len) = 0;  // non-blocking, returns 0 if no data
    virtual void Close() = 0;
};

} // namespace gt_esmini
