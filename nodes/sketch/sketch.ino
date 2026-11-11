#include <WiFi.h>
#include <esp_system.h>

#include "../../common/protocol.h"
#include "../config.h"

#include "node_dht.h"
#include "node_mqtt.h"
#include "node_spool.h"
#include "node_udp.h"

#include <string>
#include <ctime>
#include <variant>

using namespace common::protocol;

std::string sessionId;
uint32_t sequenceNumber = 0;
bool timeSynchronized = false;

// Status rolling window (one per link, so a reading delivered on one link is not resent on
// it when only the other link was down). Kept in SPIFFS: survives a restart.
Spool mqttSpool("/spool_mqtt.bin", node::spool_capacity);
Spool udpSpool("/spool_udp.bin", node::spool_capacity);

// Readings replayed per loop iteration and link, so a long backlog does not stall the
// sensor loop or flood the MQTT outbox.
constexpr int replay_batch = 5;

// Declared before any function: the Arduino builder inserts auto-generated prototypes
// above the first function definition.
enum class UdpResult { Delivered, Rejected, Failed };

std::string currentTimestamp() {
    struct tm timeinfo{};
    if (!getLocalTime(&timeinfo, 5000)) { // Increased timeout to 5 seconds
        Serial.println("getLocalTime failed");
        return {};
    }

    char timestamp[21]{};
    if (strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", &timeinfo) == 0) {
        Serial.println("strftime failed");
        return {};
    }

    return timestamp;
}

// Builds and encodes the next reading. Returns an empty string if no reading is available.
std::string buildReading() {

    if (sessionId.empty()) {
        sessionId = std::string("boot-") + std::to_string(esp_random());
        return {};
    }

    float temperature = 0;
    float humidity = 0;
    if (!dhtRead(temperature, humidity)) {
        return {};
    }

    const std::string timestamp = currentTimestamp();

    if (timestamp.empty()) {
        Serial.println("Could not get synchronized timestamp");
        return {};
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

        Serial.println("\nNew reading:");
        Serial.println(datagram.c_str());
        return datagram;
    }
    catch (const ProtocolError& error) {
        Serial.print("Protocol error: ");
        Serial.println(error.what());
    }
    catch (const std::exception& error) {
        Serial.print("Error: ");
        Serial.println(error.what());
    }
    return {};
}

bool wifiUp() {
    return WiFi.status() == WL_CONNECTED;
}

// ---- MQTT (Command Centre) ----------------------------------------------------------------

bool mqttUp() {
    return wifiUp() && mqttIsConnected();
}

void deliverMqtt(const std::string& datagram) {
    // While a backlog exists the new reading joins it, so readings arrive in order.
    if (mqttUp() && mqttSpool.empty() && mqttPublishReading(datagram)) {
        return;
    }
    Serial.println("[mqtt] link down or backlog pending: buffering reading");
    mqttSpool.push(datagram);
}

void replayMqtt() {
    for (int i = 0; i < replay_batch && !mqttSpool.empty() && mqttUp(); ++i) {
        std::string datagram;
        if (!mqttSpool.peek(datagram)) {
            Serial.println("[mqtt] dropping corrupt buffered reading");
            mqttSpool.pop();
            continue;
        }
        if (!mqttPublishReading(datagram)) {
            break;   // outbox full or client gone: retry next loop
        }
        mqttSpool.pop();
        Serial.printf("[mqtt] replayed buffered reading, %u left\n", mqttSpool.size());
    }
}

// ---- UDP (alert server) -------------------------------------------------------------------

// Sends one reading. With QoS AtLeastOnce the server ACKs every reading, so delivery is
// confirmed by the ACK; with BestEffort the server never answers, so only local failures
// (WiFi down, DNS/socket error) can be detected.
UdpResult udpDeliver(const std::string& datagram) {
    if (!wifiUp()) {
        return UdpResult::Failed;
    }

    Identity identity;
    Qos qos;
    try {
        const Message message = decode(datagram);
        const auto* reading = std::get_if<Reading>(&message);
        if (reading == nullptr) {
            Serial.println("[udp] buffered datagram is not a reading, dropping");
            return UdpResult::Rejected;
        }
        identity = reading->identity;
        qos = reading->qos;
    }
    catch (const std::exception& error) {
        Serial.print("[udp] undecodable datagram, dropping: ");
        Serial.println(error.what());
        return UdpResult::Rejected;
    }

    if (!udpSend(datagram)) {
        return UdpResult::Failed;
    }
    if (qos != Qos::AtLeastOnce) {
        return UdpResult::Delivered;
    }

    const unsigned long start = millis();
    while (millis() - start < node::config.response_timeout_ms) {
        switch (udpReceiveResponse(identity.session_id, identity.seq)) {
            case UdpResponse::Ack:
                return UdpResult::Delivered;
            case UdpResponse::Reject:
                return UdpResult::Rejected;   // resending would be rejected again
            default:
                break;
        }
        delay(10);
    }
    Serial.println("[udp] no ACK from alert server");
    return UdpResult::Failed;
}

void deliverUdp(const std::string& datagram) {
    if (udpSpool.empty() && udpDeliver(datagram) != UdpResult::Failed) {
        return;
    }
    Serial.println("[udp] link down or backlog pending: buffering reading");
    udpSpool.push(datagram);
}

void replayUdp() {
    for (int i = 0; i < replay_batch && !udpSpool.empty() && wifiUp(); ++i) {
        std::string datagram;
        if (!udpSpool.peek(datagram)) {
            Serial.println("[udp] dropping corrupt buffered reading");
            udpSpool.pop();
            continue;
        }
        if (udpDeliver(datagram) == UdpResult::Failed) {
            break;   // still down: keep it and retry next loop
        }
        udpSpool.pop();
        Serial.printf("[udp] replayed buffered reading, %u left\n", udpSpool.size());
    }
}

void setup() {

    Serial.begin(node::config.serial_baud);

    // Mount SPIFFS first: readings buffered before a restart are replayed once the links
    // are back. Failure is not fatal; the node then runs without a rolling window.
    if (spoolMount()) {
        mqttSpool.begin();
        udpSpool.begin();
    }

    Serial.println("Connecting to WiFi...");

    WiFi.setAutoReconnect(true);
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

    const std::string datagram = buildReading();

    if (!datagram.empty()) {
        // Same JSON to the Command Centre; it accepts the UDP envelope as-is.
        deliverMqtt(datagram);
        deliverUdp(datagram);
    }

    // Send what was buffered during an outage (or before a restart) once a link is back.
    replayMqtt();
    replayUdp();

    if (!mqttSpool.empty() || !udpSpool.empty()) {
        Serial.printf("[spool] pending: mqtt=%u udp=%u\n", mqttSpool.size(), udpSpool.size());
    }

    delay(node::config.reading_delay_ms);
}
