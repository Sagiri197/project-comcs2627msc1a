#pragma once

// POSIX sockets (Linux / Raspberry Pi) implementation of IUdpSocket.

#include "udp_socket.h"

class PosixUdpSocket : public IUdpSocket {
public:
    explicit PosixUdpSocket(const UdpEndpoint& endpoint = {"0.0.0.0", 0});
    ~PosixUdpSocket() override;

    void send(std::string_view data, const UdpEndpoint& destination) const override;
    UdpDatagram recv() const override;

private:
    int m_descriptor;
};
