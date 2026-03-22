#pragma once

#include "gt_esmini/control/manualdrive/ITransport.hpp"

#ifdef _WIN32
#include <winsock2.h>
#include <Ws2tcpip.h>
typedef SOCKET socket_t;
#define INVALID_SOCK INVALID_SOCKET
#else
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
typedef int socket_t;
#define INVALID_SOCK (-1)
#endif

#include <vector>

namespace gt_esmini
{

class TcpTransport : public ITransport
{
public:
    TcpTransport();
    ~TcpTransport();

    bool Open(const TransportConfig& config) override;
    int  Send(const void* data, size_t len) override;
    int  Recv(void* buf, size_t max_len) override;
    void Close() override;

private:
    bool OpenAsServer(int port);
    bool OpenAsClient(const std::string& host, int port);
    bool AcceptConnection();
    bool SendExact(const void* data, size_t len);
    bool RecvExact(void* buf, size_t len);

    socket_t listen_sock_ = INVALID_SOCK;
    socket_t conn_sock_   = INVALID_SOCK;
    bool     is_server_   = true;
    bool     connected_   = false;

    // Receive buffer for length-prefixed messages
    std::vector<char> recv_buf_;
    int               pending_msg_len_ = -1;  // -1 = reading header
};

} // namespace gt_esmini
