#include "node_udp.h"

#include <Arduino.h>
#include <WiFiUdp.h>

#include "../../common/protocol.h"
#include "../config.h"

#include <variant>

using namespace common::protocol;

namespace {
WiFiUDP udp;
}

void udpBegin() {
    if (udp.begin(node::config.local_port)) {
        Serial.println("UDP socket initialized!");
    } else {
        Serial.println("UDP initialization failed!");
    }
}

bool udpSend(const std::string& datagram) {
    if (!udp.beginPacket(node::config.server_address, node::config.server_port)) {
        Serial.println("Could not initialize UDP packet");
        return false;
    }

    udp.write(
        reinterpret_cast<const uint8_t*>(datagram.data()),
        datagram.size()
    );

    if (udp.endPacket()) {
        Serial.println("Reading transmitted successfully");
        return true;
    }
    Serial.println("UDP transmission failed");
    return false;
}

UdpResponse udpReceiveResponse(const std::string& sessionId, std::uint32_t sequenceNumber) {

    int packetSize = udp.parsePacket();

    if (packetSize <= 0) {
        return UdpResponse::None;
    }

    if (packetSize > max_datagram_size) {
        Serial.println("Received oversized datagram");
        udp.flush();
        return UdpResponse::Other;
    }

    char buffer[max_datagram_size + 1];

    int len = udp.read(
        reinterpret_cast<uint8_t*>(buffer),
        max_datagram_size
    );

    if (len <= 0) {
        return UdpResponse::None;
    }

    buffer[len] = '\0';

    Serial.println("\nReceived server response:");
    Serial.println(buffer);

    try {

        Message response = decode(
            std::string_view(buffer, len)
        );

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
                return UdpResponse::Ack;
            }
            Serial.println("ACK does not match current reading!");
            return UdpResponse::Other;

        }
        else if (const auto* reject =
                     std::get_if<Reject>(&response)) {

            Serial.println("SERVER REJECTED MESSAGE");

            Serial.print("Reason: ");
            Serial.println(reject->reason.c_str());

            if (reject->identity.node_id == node::config.node_id &&
                reject->identity.session_id == sessionId &&
                reject->identity.seq == sequenceNumber) {
                return UdpResponse::Reject;
            }
            return UdpResponse::Other;

        }
        else {
            Serial.println("Unexpected response type");
        }

    }
    catch (const ProtocolError& error) {

        Serial.print("Failed to decode response: ");
        Serial.println(error.what());

    }
    catch (const std::exception& error) {

        Serial.print("Error processing response: ");
        Serial.println(error.what());

    }
    return UdpResponse::Other;
}
