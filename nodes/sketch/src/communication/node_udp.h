#pragma once

#include <WiFiUdp.h>

#include <cstdint>
#include <string>

class UdpTransport {
public:
    UdpTransport(std::uint16_t localPort, const char* serverAddress,
                 std::uint16_t serverPort);

    bool begin();
    bool send(const std::string& datagram);
    void receiveResponse(const std::string& sessionId, std::uint32_t sequenceNumber);

private:
    WiFiUDP m_socket;
    std::uint16_t m_localPort;
    const char* m_serverAddress;
    std::uint16_t m_serverPort;
};