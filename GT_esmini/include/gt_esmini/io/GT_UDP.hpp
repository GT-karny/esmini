#pragma once

#include <string>

// UDP network includes
#ifdef _WIN32
#include <winsock2.h>
#include <Ws2tcpip.h>
#else
/* Assume that any non-Windows platform uses POSIX-style sockets instead. */
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>  /* Needed for getaddrinfo() and freeaddrinfo() */
#include <unistd.h> /* Needed for close() */
#endif

namespace gt_esmini
{

    class GT_UDP_Sender
    {
    public:
        GT_UDP_Sender(unsigned short int port, std::string ipAddress);
        ~GT_UDP_Sender();

        // Returns number of bytes sent, or -1 on error
        int Send(const char* buf, unsigned int size);

        unsigned short GetPort() const
        {
            return port_;
        }
        std::string GetIPAddress() const
        {
            return ipAddress_;
        }

    private:
        void CloseGracefully();

        unsigned short int port_;
        std::string        ipAddress_;

#ifdef _WIN32
        SOCKET sock_;
#else
        int sock_;
#endif
        struct sockaddr_in server_addr_;
    };

} // namespace gt_esmini
