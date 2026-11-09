#include "node_udp.h"

#include <Arduino.h>

#include "../../../../common/protocol.h"
#include "../../../config.h"

#include <variant>

using namespace common::protocol;

UdpTransport::UdpTransport(std::uint16_t localPort, const char* serverAddress,
                           std::uint16_t serverPort)
    : m_localPort(localPort), m_serverAddress(serverAddress), m_serverPort(serverPort) {}

bool UdpTransport::begin() {
    if (m_socket.begin(m_localPort)) {
        Serial.println("UDP socket initialized!");
        return true;
    }
    Serial.println("UDP initialization failed!");
    return false;
}

bool UdpTransport::send(const std::string& datagram) {
    if (!m_socket.beginPacket(m_serverAddress, m_serverPort)) {
        Serial.println("Could not initialize UDP packet");
        return false;
    }

    m_socket.write(reinterpret_cast<const std::uint8_t*>(datagram.data()), datagram.size());

    if (m_socket.endPacket()) {
        Serial.println("Reading transmitted successfully");
        return true;
    }
    Serial.println("UDP transmission failed");
    return false;
}

void UdpTransport::receiveResponse(const std::string& sessionId,
                                   std::uint32_t sequenceNumber) {
    const int packetSize = m_socket.parsePacket();
    if (packetSize <= 0) {
        return;
    }
    if (packetSize > max_datagram_size) {
        Serial.println("Received oversized datagram");
        return;
    }

    char buffer[max_datagram_size + 1];
    const int length = m_socket.read(reinterpret_cast<std::uint8_t*>(buffer), max_datagram_size);
    if (length <= 0) {
        return;
    }
    buffer[length] = '\0';

    Serial.println("\nReceived server response:");
    Serial.println(buffer);

    try {
        Message response = decode(std::string_view(buffer, length));
        if (const auto* ack = std::get_if<Ack>(&response)) {
            Serial.println("ACK received!");
            Serial.print("Node ID: ");
            Serial.println(ack->identity.node_id.c_str());
            Serial.print("Session ID: ");
            Serial.println(ack->identity.session_id.c_str());
            Serial.print("Sequence: ");
            Serial.println(ack->identity.seq);

            if (ack->identity.node_id == node::config.node_id &&
                ack->identity.session_id == sessionId &&
                ack->identity.seq == sequenceNumber) {
                Serial.println("ACK matches current reading!");
            } else {
                Serial.println("ACK does not match current reading!");
            }
        } else if (const auto* reject = std::get_if<Reject>(&response)) {
            Serial.println("SERVER REJECTED MESSAGE");
            Serial.print("Reason: ");
            Serial.println(reject->reason.c_str());
        } else {
            Serial.println("Unexpected response type");
        }
    } catch (const ProtocolError& error) {
        Serial.print("Failed to decode response: ");
        Serial.println(error.what());
    } catch (const std::exception& error) {
        Serial.print("Error processing response: ");
        Serial.println(error.what());
    }
}