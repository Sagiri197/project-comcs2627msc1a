#pragma once

// Windows (Winsock) variant of udp_socket.h, with the same interface. On Windows, CMake copies
// this file into the build tree as udp_socket.h, so the rest of the code includes it unchanged.

#include <cstdint>
#include <string>
#include <string_view>

struct UdpEndpoint {
    std::string address;
    std::uint16_t port;
};

struct UdpDatagram {
    std::string data;
    UdpEndpoint sender;
};

class UdpSocket {
public:
    explicit UdpSocket(const UdpEndpoint& endpoint = {"0.0.0.0", 0});
    ~UdpSocket();

    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;

    void send(std::string_view data, const UdpEndpoint& destination) const;
    UdpDatagram recv() const;

private:
    std::uintptr_t m_socket;   // Winsock SOCKET, kept out of the header to avoid <winsock2.h>
};
