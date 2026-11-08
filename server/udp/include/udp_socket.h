#pragma once

#include <cstdint>
#include <memory>
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

// A bound IPv4 UDP socket. Implemented by PosixUdpSocket (udp_socket_posix.h) and
// WindowsUdpSocket (udp_socket_windows.h); only the one for the target platform is built.
class IUdpSocket {
public:
    virtual ~IUdpSocket() = default;

    // Sends one datagram. Throws std::invalid_argument for a malformed address and
    // std::system_error if the OS rejects the send.
    virtual void send(std::string_view data, const UdpEndpoint& destination) const = 0;

    // Blocks until a datagram arrives. Throws std::system_error on failure.
    virtual UdpDatagram recv() const = 0;

protected:
    IUdpSocket() = default;
    IUdpSocket(const IUdpSocket&) = delete;
    IUdpSocket& operator=(const IUdpSocket&) = delete;
};

// Creates the socket implementation for the platform being built, bound to `endpoint`.
std::unique_ptr<IUdpSocket> make_udp_socket(const UdpEndpoint& endpoint = {"0.0.0.0", 0});
