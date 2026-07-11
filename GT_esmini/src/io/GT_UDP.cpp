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

    GT_UDP_Sender::GT_UDP_Sender(unsigned short int port, std::string ipAddress)
        : port_(port), ipAddress_(ipAddress), sock_(SE_INVALID_SOCKET)
    {
#ifdef _WIN32
        WSADATA wsa_data;
        int     iResult = WSAStartup(MAKEWORD(2, 2), &wsa_data);
        if (iResult != NO_ERROR)
        {
            LOG_ERROR("GT_UDP_Sender: WSAStartup failed with error {}", iResult);
            return;
        }
        wsa_started_ = true;
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

#ifdef _WIN32
        // Only undo our own successful WSAStartup; an unconditional WSACleanup would
        // over-decrement the process-wide refcount and break sockets still owned by others.
        if (wsa_started_)
        {
            WSACleanup();
            wsa_started_ = false;
        }
#endif
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
