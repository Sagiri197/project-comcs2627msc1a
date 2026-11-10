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

/** @brief Bound IPv4 UDP socket abstraction used by the server. */
class IUdpSocket {
public:
    virtual ~IUdpSocket() = default;

    /**
     * @brief Sends one datagram.
     * @throws std::invalid_argument if the destination address is malformed.
     * @throws std::system_error if the operating system rejects the send.
     */
    virtual void send(std::string_view data, const UdpEndpoint& destination) const = 0;

    /** @brief Blocks until a datagram arrives; throws std::system_error on failure. */
    virtual UdpDatagram recv() const = 0;

protected:
    IUdpSocket() = default;
    IUdpSocket(const IUdpSocket&) = delete;
    IUdpSocket& operator=(const IUdpSocket&) = delete;
};

/** @brief Creates the platform-specific IPv4 socket bound to `endpoint`. */
std::unique_ptr<IUdpSocket> make_udp_socket(const UdpEndpoint& endpoint = {"0.0.0.0", 0});
