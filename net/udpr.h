#pragma once

#include <string>
#include <functional>
#include <thread>
#include <atomic>
#include <stdexcept>
#include <cstring>
#include <iostream>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    using SocketHandle = SOCKET;
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <errno.h>
    using SocketHandle = int;
#endif

namespace UDP {

class Receiver {
public:
    using Callback = std::function<void(const std::string& message)>;

    Receiver(uint16_t port, Callback callback)
        : port(port), callback(callback), running(true)
    {
#ifdef _WIN32
        WSADATA statusData;
        WSAStartup(MAKEWORD(2,2), &statusData);
#endif
        sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (sock < 0) {
            throw std::runtime_error("Failed to create UDP socket");
        }

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = INADDR_ANY;

        int opt = 1;
        setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<char*>(&opt), sizeof(opt));

        if (bind(sock, (sockaddr*)&addr, sizeof(addr)) < 0) {
            closeSocket();
            throw std::runtime_error("Failed to bind UDP socket");
        }

        listener = std::thread([this]() { this->loop(); });
    }

    ~Receiver() {
        running = false;
        if (listener.joinable()) listener.join();
        closeSocket();
#ifdef _WIN32
        WSACleanup();
#endif
    }

private:
    void loop() {
        while (running) {
            std::string buffer;
            buffer.resize(1024);
            sockaddr_in from{};
            socklen_t fromLen = sizeof(from);

            int len = recvfrom(sock, &buffer[0], buffer.size(), 0,
                               (sockaddr*)&from, &fromLen);

            if (len > 0) {
                buffer.resize(len); // trim to actual size
                callback(buffer);
            } else if (len == -1) {
#ifdef _WIN32
                int err = WSAGetLastError();
                if (err == WSAEINTR) continue;
#else
                if (errno == EINTR) continue;
#endif
                std::cerr << "recvfrom error, stopping listener." << std::endl;
                break;
            }
        }
    }

    void closeSocket() {
#ifdef _WIN32
        closesocket(sock);
#else
        ::close(sock);
#endif
    }

    uint16_t port;
    Callback callback;
    std::atomic<bool> running;
    std::thread listener;
    SocketHandle sock;
};

} // namespace UDP

