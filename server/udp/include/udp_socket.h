#pragma once

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
    int m_descriptor;
};