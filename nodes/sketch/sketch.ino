
#include <WiFi.h>
#include <esp_system.h>

#include "../../common/protocol.h"
#include "../config.h"

#include "node_dht.h"
#include "node_mqtt.h"
#include "node_udp.h"

#include <string>
#include <ctime>

using namespace common::protocol;

std::string sessionId;
uint32_t sequenceNumber = 0;
bool timeSynchronized = false;

std::string currentTimestamp() {
    struct tm timeinfo{};
    if (!getLocalTime(&timeinfo, 1000)) {
        return {};
    }

    char timestamp[21]{};
    if (strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", &timeinfo) == 0) {
        return {};
    }
    return timestamp;
}

void sendReading() {

    if (sessionId.empty()) {
        sessionId = std::string("boot-") + std::to_string(esp_random());
        return;
    }

    float temperature = 0;
    float humidity = 0;
    if (!dhtRead(temperature, humidity)) {
        return;
    }

    const std::string timestamp = currentTimestamp();
    if (timestamp.empty()) {
        Serial.println("Could not get synchronized timestamp");
        return;
    }

    sequenceNumber++;

    Serial.print(node::config.node_id);

    Identity identity {
        node::config.node_id,
        sessionId,
        sequenceNumber
    };

    WeatherObserved weather {
        // Unique per reading: the Command Centre discards repeated ids as duplicates.
        std::string("urn:ngsi-ld:WeatherObserved:") + node::config.node_id + ":" +
            sessionId + ":" + std::to_string(sequenceNumber),
        timestamp,
        {
            node::config.longitude,
            node::config.latitude
        },
        std::string("urn:ngsi-ld:Device:") + node::config.node_id,
        temperature,
        humidity / 100.0
    };

    Reading reading {
        identity,
        node::config.qos,
        weather,
        timeSynchronized
    };

    Message message = reading;

    try {
        std::string datagram = encode(message);

        Serial.println("\nSending reading:");
        Serial.println(datagram.c_str());

        // Same JSON to the Command Centre; it accepts the UDP envelope as-is.
        mqttPublishReading(datagram);

        udpSend(datagram);
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

void setup() {

    Serial.begin(node::config.serial_baud);

    Serial.println("Connecting to WiFi...");

    WiFi.begin(node::config.wifi_ssid, node::config.wifi_password);

    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }

    Serial.println("\nWiFi connected!");

    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    struct tm timeinfo{};
    timeSynchronized = getLocalTime(&timeinfo, node::config.time_sync_threshold_ms);
    Serial.println(timeSynchronized ? "Time synchronized!" : "Time synchronization failed");

    dhtBegin();

    Serial.print("ESP32 IP: ");
    Serial.println(WiFi.localIP());

    udpBegin();

    mqttStart();
}

void loop() {

    sendReading();

    // Wait briefly for a response
    unsigned long start = millis();

    while (millis() - start < node::config.response_timeout_ms) {

        udpReceiveResponse(sessionId, sequenceNumber);

        delay(10);
    }

    delay(node::config.reading_delay_ms);
}
