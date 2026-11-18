#pragma once

#include <WiFiUdp.h>

#include <cstdint>
#include <string>

/** @brief What a poll for the alert server's response found. */
enum class UdpResponse {
    None,    ///< Nothing received yet.
    Ack,     ///< ACK for the reading being waited on.
    Reject,  ///< Reject for the reading being waited on (resending would be rejected again).
    Other    ///< Something else (another reading's response, undecodable datagram, ...).
};

class UdpTransport {
public:
    UdpTransport(std::uint16_t localPort, const char* serverAddress,
                 std::uint16_t serverPort);

    bool begin();
    bool send(const std::string& datagram);
    UdpResponse receiveResponse(const std::string& sessionId, std::uint32_t sequenceNumber);

private:
    WiFiUDP m_socket;
    std::uint16_t m_localPort;
    const char* m_serverAddress;
    std::uint16_t m_serverPort;
};
