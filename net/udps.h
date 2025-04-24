#pragma once

#include <string>
#include <cstring>
#include <stdexcept>
#include <cstdint> 

namespace UDP {

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    using SocketHandle = SOCKET;

    inline void init() { 
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2,2), &wsaData) != 0) {
            throw std::runtime_error("WSAStartup failed");
        }
    }
    inline void shutdown()   { WSACleanup(); }
    inline void close(SocketHandle s) { closesocket(s); }

#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>

    using SocketHandle = int;

    inline void init()       {}
    inline void shutdown()   {}
    inline void close(SocketHandle s) { ::close(s); }
#endif

class Socket {
public:
    std::string ip;
    uint16_t port;

    Socket(const std::string& targetIp, uint16_t targetPort)
        : ip(targetIp), port(targetPort)
    {
        init();

        sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (sock < 0) throw std::runtime_error("Failed to create socket");

        std::memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) <= 0) {
            throw std::runtime_error("Invalid IP address format");
        }
    }

    void send(const std::string& message) const {
        int result = sendto(sock, message.c_str(), message.size(), 0,
                            (sockaddr*)&addr, sizeof(addr));
        if (result < 0) throw std::runtime_error("UDP send failed");
    }

    void close() {
        UDP::close(sock);
        shutdown();
    }

    ~Socket() {
        close();
    }

private:
    SocketHandle sock;
    sockaddr_in addr{};
};

} // namespace UDP

