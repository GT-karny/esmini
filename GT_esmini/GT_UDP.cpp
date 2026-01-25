#include "GT_UDP.hpp"
#include <iostream>
#include <stdio.h>
#include <cstring> // for memset

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

// Helper macro for logging (simplified for this standalone class or utilize existing logger if possible)
// Using std::cerr/cout for now to minimize dependencies, or we can assume logger.hpp is available if we include it.
// Given strict instructions to "avoid dependency", keeping it minimal. 
// But commonly this project uses logger.hpp. Let's include standard headers.

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
            std::cerr << "GT_UDP_Sender: WSAStartup failed with error " << iResult << std::endl;
            return;
        }
#endif

        if ((sock_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == SE_INVALID_SOCKET)
        {
#ifdef _WIN32
            std::cerr << "GT_UDP_Sender: socket failed with error " << WSAGetLastError() << std::endl;
#else
            perror("GT_UDP_Sender: socket failed");
#endif
            return;
        }

        // Prepare the sockaddr_in structure
        std::memset(reinterpret_cast<char*>(&server_addr_), 0, sizeof(server_addr_));
        server_addr_.sin_family = AF_INET;
        server_addr_.sin_port   = htons(port_);
        
#ifdef _WIN32
        if (inet_pton(AF_INET, ipAddress.c_str(), &server_addr_.sin_addr.s_addr) != 1) {
             std::cerr << "GT_UDP_Sender: Invalid IP address format: " << ipAddress << std::endl;
        }
#else
        if (inet_pton(AF_INET, ipAddress.c_str(), &server_addr_.sin_addr.s_addr) != 1) {
             std::cerr << "GT_UDP_Sender: Invalid IP address format: " << ipAddress << std::endl;
        }
#endif
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
                std::cerr << "GT_UDP_Sender: Failed closing socket " << WSAGetLastError() << std::endl;
#else
                perror("GT_UDP_Sender: close socket");
#endif
            }
            sock_ = SE_INVALID_SOCKET;
        }

#ifdef _WIN32
        WSACleanup();
#endif
    }

    int GT_UDP_Sender::Send(const char* buf, unsigned int size)
    {
        if (sock_ == SE_INVALID_SOCKET) return -1;

        int sent = sendto(sock_, buf, size, 0, reinterpret_cast<struct sockaddr*>(&server_addr_), sizeof(server_addr_));
        
        if (sent == SE_SOCKET_ERROR)
        {
#ifdef _WIN32
             // std::cerr << "GT_UDP_Sender: sendto failed " << WSAGetLastError() << std::endl;
#else
             // perror("GT_UDP_Sender: sendto");
#endif
             return -1;
        }
        return sent;
    }

} // namespace gt_esmini
