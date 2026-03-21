#include "gt_esmini/control/racingwheel/TcpTransport.hpp"
#include "logger.hpp"

#include <cstring>

#ifdef _WIN32
#pragma comment(lib, "Ws2_32.lib")
static bool wsa_initialized = false;
static void EnsureWSA()
{
    if (!wsa_initialized)
    {
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
        wsa_initialized = true;
    }
}
#else
static void EnsureWSA() {}
#endif

namespace gt_esmini
{

TcpTransport::TcpTransport() = default;

TcpTransport::~TcpTransport()
{
    Close();
}

bool TcpTransport::Open(const TransportConfig& config)
{
    EnsureWSA();
    is_server_ = config.is_server;

    if (is_server_)
    {
        return OpenAsServer(config.listen_port);
    }
    else
    {
        return OpenAsClient(config.host, config.target_port);
    }
}

bool TcpTransport::OpenAsServer(int port)
{
    listen_sock_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_sock_ == INVALID_SOCK)
    {
        LOG_ERROR("TcpTransport: Failed to create server socket");
        return false;
    }

    // Allow address reuse
    int opt = 1;
    setsockopt(listen_sock_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(static_cast<unsigned short>(port));

    if (bind(listen_sock_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0)
    {
        LOG_ERROR("TcpTransport: Failed to bind on port {}", port);
        Close();
        return false;
    }

    if (listen(listen_sock_, 1) != 0)
    {
        LOG_ERROR("TcpTransport: Failed to listen on port {}", port);
        Close();
        return false;
    }

    // Set listen socket to non-blocking for accept
#ifdef _WIN32
    u_long mode = 1;
    ioctlsocket(listen_sock_, FIONBIO, &mode);
#else
    int flags = fcntl(listen_sock_, F_GETFL, 0);
    fcntl(listen_sock_, F_SETFL, flags | O_NONBLOCK);
#endif

    LOG_INFO("TcpTransport: Server listening on port {}", port);
    return true;
}

bool TcpTransport::OpenAsClient(const std::string& host, int port)
{
    conn_sock_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (conn_sock_ == INVALID_SOCK)
    {
        LOG_ERROR("TcpTransport: Failed to create client socket");
        return false;
    }

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<unsigned short>(port));
    inet_pton(AF_INET, host.c_str(), &addr.sin_addr);

    if (connect(conn_sock_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0)
    {
        LOG_WARN("TcpTransport: Failed to connect to {}:{}", host, port);
        // Non-fatal — will retry on next Send/Recv
        return true;
    }

    // Set connected socket to non-blocking
#ifdef _WIN32
    u_long mode = 1;
    ioctlsocket(conn_sock_, FIONBIO, &mode);
#else
    int flags = fcntl(conn_sock_, F_GETFL, 0);
    fcntl(conn_sock_, F_SETFL, flags | O_NONBLOCK);
#endif

    connected_ = true;
    LOG_INFO("TcpTransport: Connected to {}:{}", host, port);
    return true;
}

bool TcpTransport::AcceptConnection()
{
    if (listen_sock_ == INVALID_SOCK || connected_)
    {
        return connected_;
    }

    sockaddr_in client_addr = {};
    int addr_len = sizeof(client_addr);
    conn_sock_ = accept(listen_sock_, reinterpret_cast<sockaddr*>(&client_addr),
#ifdef _WIN32
                         &addr_len);
#else
                         reinterpret_cast<socklen_t*>(&addr_len));
#endif

    if (conn_sock_ == INVALID_SOCK)
    {
        return false;  // No connection yet (non-blocking)
    }

    // Set to non-blocking
#ifdef _WIN32
    u_long mode = 1;
    ioctlsocket(conn_sock_, FIONBIO, &mode);
#else
    int flags = fcntl(conn_sock_, F_GETFL, 0);
    fcntl(conn_sock_, F_SETFL, flags | O_NONBLOCK);
#endif

    connected_ = true;
    LOG_INFO("TcpTransport: Client connected");
    return true;
}

int TcpTransport::Send(const void* data, size_t len)
{
    if (!connected_)
    {
        if (is_server_) AcceptConnection();
        if (!connected_) return -1;
    }

    // Length-prefix framing: [4 bytes big-endian length][payload]
    uint32_t net_len = htonl(static_cast<uint32_t>(len));
    if (!SendExact(&net_len, 4))
    {
        return -1;
    }
    if (!SendExact(data, len))
    {
        return -1;
    }
    return static_cast<int>(len);
}

int TcpTransport::Recv(void* buf, size_t max_len)
{
    if (!connected_)
    {
        if (is_server_) AcceptConnection();
        if (!connected_) return 0;
    }

    // Try to read length header first
    if (pending_msg_len_ < 0)
    {
        uint32_t net_len = 0;
        int r = recv(conn_sock_, reinterpret_cast<char*>(&net_len), 4, 0);
        if (r <= 0)
        {
            return 0;  // No data available (non-blocking)
        }
        if (r < 4)
        {
            return 0;  // Partial header — simplified handling
        }
        pending_msg_len_ = static_cast<int>(ntohl(net_len));

        if (pending_msg_len_ <= 0 || pending_msg_len_ > 1024 * 1024)
        {
            // Invalid length — reset
            pending_msg_len_ = -1;
            return 0;
        }
    }

    // Read payload
    size_t to_read = static_cast<size_t>(pending_msg_len_);
    if (to_read > max_len)
    {
        to_read = max_len;  // Truncate
    }

    int r = recv(conn_sock_, static_cast<char*>(buf), static_cast<int>(to_read), 0);
    if (r > 0)
    {
        pending_msg_len_ = -1;  // Reset for next message
        return r;
    }

    return 0;
}

bool TcpTransport::SendExact(const void* data, size_t len)
{
    const char* ptr = static_cast<const char*>(data);
    size_t sent = 0;
    while (sent < len)
    {
        int r = send(conn_sock_, ptr + sent, static_cast<int>(len - sent), 0);
        if (r <= 0)
        {
            connected_ = false;
            return false;
        }
        sent += static_cast<size_t>(r);
    }
    return true;
}

bool TcpTransport::RecvExact(void* buf, size_t len)
{
    char* ptr = static_cast<char*>(buf);
    size_t received = 0;
    while (received < len)
    {
        int r = recv(conn_sock_, ptr + received, static_cast<int>(len - received), 0);
        if (r <= 0)
        {
            return false;
        }
        received += static_cast<size_t>(r);
    }
    return true;
}

void TcpTransport::Close()
{
    auto close_sock = [](socket_t& s) {
        if (s != INVALID_SOCK)
        {
#ifdef _WIN32
            closesocket(s);
#else
            close(s);
#endif
            s = INVALID_SOCK;
        }
    };

    close_sock(conn_sock_);
    close_sock(listen_sock_);
    connected_ = false;
    pending_msg_len_ = -1;
}

} // namespace gt_esmini
