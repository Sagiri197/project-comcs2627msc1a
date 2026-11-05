
#include <WiFi.h>
#include <WiFiUdp.h>

#include "src/common/protocol.h"
#include "config.h"

#include <string>
#include <variant>

using namespace common::protocol;

WiFiUDP udp;

std::string sessionId;
uint32_t sequenceNumber = 0;

void sendReading() {

    if (sessionId.empty()) {
        sessionId = std::string("urn:ngsi-ld:Session:1");
        return;
    }

    sequenceNumber++;

    Identity identity {
        node::config.node_id,
        sessionId,
        sequenceNumber
    };

    WeatherObserved weather {
        std::string("urn:ngsi-ld:WeatherObserved:") + node::config.node_id,
        node::config.sample_timestamp,
        {
            node::config.longitude,
            node::config.latitude
        },
        std::string("urn:ngsi-ld:Device:") + node::config.node_id,
        node::config.sample_temperature,
        node::config.sample_relative_humidity
    };

    Reading reading {
        identity,
        node::config.qos,
        weather,
        node::config.time_synchronized
    };

    Message message = reading;

    try {

        // Serialize using your protocol implementation
        std::string datagram = encode(message);

        Serial.println("\nSending reading:");
        Serial.println(datagram.c_str());

        if (!udp.beginPacket(node::config.server_address, node::config.server_port)) {
            Serial.println("Could not initialize UDP packet");
            return;
        }

        udp.write(
            reinterpret_cast<const uint8_t*>(datagram.data()),
            datagram.size()
        );

        if (udp.endPacket()) {
            Serial.println("Reading transmitted successfully");
        } else {
            Serial.println("UDP transmission failed");
        }

    }
    catch (const ProtocolError& error) {
        Serial.print("Protocol error: ");
        Serial.println(error.what());
    }
    catch (const std::exception& error) {
        Serial.print("Error: ");
        Serial.println(error.what());
    }
}

void receiveResponse() {

    int packetSize = udp.parsePacket();

    if (packetSize <= 0) {
        return;
    }

    if (packetSize > max_datagram_size) {
        Serial.println("Received oversized datagram");
        return;
    }

    char buffer[max_datagram_size + 1];

    int len = udp.read(
        reinterpret_cast<uint8_t*>(buffer),
        max_datagram_size
    );

    if (len <= 0) {
        return;
    }

    buffer[len] = '\0';

    Serial.println("\nReceived server response:");
    Serial.println(buffer);

    try {

        // Decode using your protocol implementation
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

            } else {
                Serial.println("ACK does not match current reading!");
            }

        }
        else if (const auto* reject =
                     std::get_if<Reject>(&response)) {

            Serial.println("SERVER REJECTED MESSAGE");

            Serial.print("Reason: ");
            Serial.println(reject->reason.c_str());

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
}

void setup() {

    Serial.begin(node::config.serial_baud);

    Serial.println("Connecting to WiFi...");

    WiFi.begin(node::config.wifi_ssid, node::config.wifi_password);

    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }

    Serial.println("\nWiFi connected!");

    Serial.print("ESP32 IP: ");
    Serial.println(WiFi.localIP());

    // Initialize UDP socket for sending and receiving
    if (udp.begin(node::config.local_port)) {
        Serial.println("UDP socket initialized!");
    } else {
        Serial.println("UDP initialization failed!");
    }

}

void loop() {

    sendReading();

    // Wait briefly for a response
    unsigned long start = millis();

    while (millis() - start < node::config.response_timeout_ms) {

        receiveResponse();

        delay(10);
    }

    delay(node::config.reading_delay_ms);
}
