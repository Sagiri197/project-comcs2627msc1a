#pragma once

// Windows (Winsock) implementation of IUdpSocket.

#include "udp_socket.h"

#include <cstdint>

class WindowsUdpSocket : public IUdpSocket {
public:
    explicit WindowsUdpSocket(const UdpEndpoint& endpoint = {"0.0.0.0", 0});
    ~WindowsUdpSocket() override;

    void send(std::string_view data, const UdpEndpoint& destination) const override;
    UdpDatagram recv() const override;

private:
    std::uintptr_t m_socket;   // Winsock SOCKET, kept out of the header to avoid <winsock2.h>
};
