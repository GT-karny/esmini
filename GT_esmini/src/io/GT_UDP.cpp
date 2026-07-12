#include "gt_esmini/io/GT_UDP.hpp"
#include "logger.hpp"
#include <cerrno>
#include <cstring> // for memset, strerror

#ifndef _WIN32
#include <sys/time.h>
#endif

#ifdef _WIN32
#define SE_INVALID_SOCKET INVALID_SOCKET
#define SE_SOCKET_ERROR SOCKET_ERROR
#else
#define SE_INVALID_SOCKET -1
#define SE_SOCKET_ERROR -1
#endif

namespace gt_esmini
{

#ifdef _WIN32
    namespace
    {
        // Process-wide Winsock initializer (replaces the previous per-instance
        // WSAStartup/WSACleanup pairing).
        //
        // WSAStartup/WSACleanup are refcounted by the OS; we take exactly ONE reference
        // for the whole process via a function-local static (thread-safe init since C++11)
        // and deliberately never call WSACleanup. Winsock therefore stays initialized for
        // the lifetime of the process and is reclaimed by the OS at exit — the same
        // "leave teardown to the OS" policy adopted for sockets in GT-6 (fb749dfc /
        // cf5673df). This structurally prevents a GT_UDP_Sender's closesocket() from ever
        // failing with WSANOTINITIALISED (10093) during static destruction, since Winsock
        // is never de-initialized while GT still owns sockets.
        struct WinsockContext
        {
            bool ok = false;
            WinsockContext()
            {
                WSADATA wsa_data;
                int     iResult = WSAStartup(MAKEWORD(2, 2), &wsa_data);
                ok              = (iResult == NO_ERROR);
                if (!ok)
                {
                    LOG_ERROR("GT_UDP_Sender: WSAStartup failed with error {}", iResult);
                }
            }
            // Intentionally NO destructor / WSACleanup — see note above.
        };

        bool EnsureWinsockInitialized()
        {
            static WinsockContext ctx;
            return ctx.ok;
        }
    }  // namespace
#endif

    GT_UDP_Sender::GT_UDP_Sender(unsigned short int port, std::string ipAddress)
        : port_(port), ipAddress_(ipAddress), sock_(SE_INVALID_SOCKET)
    {
#ifdef _WIN32
        if (!EnsureWinsockInitialized())
        {
            return;  // WSAStartup failed process-wide; sock_ stays invalid.
        }
#endif

        if ((sock_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == SE_INVALID_SOCKET)
        {
#ifdef _WIN32
            LOG_ERROR("GT_UDP_Sender: socket failed with error {}", WSAGetLastError());
#else
            LOG_ERROR("GT_UDP_Sender: socket failed: {}", strerror(errno));
#endif
            return;
        }

        // Prepare the sockaddr_in structure
        std::memset(reinterpret_cast<char*>(&server_addr_), 0, sizeof(server_addr_));
        server_addr_.sin_family = AF_INET;
        server_addr_.sin_port   = htons(port_);

        if (inet_pton(AF_INET, ipAddress.c_str(), &server_addr_.sin_addr.s_addr) != 1)
        {
            LOG_ERROR("GT_UDP_Sender: Invalid IP address format: {}", ipAddress);
        }
    }

    GT_UDP_Sender::~GT_UDP_Sender()
    {
        CloseGracefully();
    }

    void GT_UDP_Sender::CloseGracefully()
    {
        if (sock_ != SE_INVALID_SOCKET)
        {
#ifdef _WIN32
            if (closesocket(sock_) == SE_SOCKET_ERROR)
#else
            if (close(sock_) < 0)
#endif
            {
#ifdef _WIN32
                LOG_WARN("GT_UDP_Sender: Failed closing socket {}", WSAGetLastError());
#else
                LOG_WARN("GT_UDP_Sender: Failed closing socket: {}", strerror(errno));
#endif
            }
            sock_ = SE_INVALID_SOCKET;
        }

        // No WSACleanup here: Winsock is initialized once per process by WinsockContext
        // and left to the OS to reclaim at exit (see the note above the singleton). This
        // avoids over-decrementing the process-wide Winsock refcount and the GT-6
        // WSANOTINITIALISED-during-teardown hazard.
    }

    int GT_UDP_Sender::Send(const char* buf, unsigned int size)
    {
        if (sock_ == SE_INVALID_SOCKET) return -1;

        int sent = sendto(sock_, buf, size, 0, reinterpret_cast<struct sockaddr*>(&server_addr_), sizeof(server_addr_));

        if (sent == SE_SOCKET_ERROR)
        {
            return -1;
        }
        return sent;
    }

} // namespace gt_esmini
