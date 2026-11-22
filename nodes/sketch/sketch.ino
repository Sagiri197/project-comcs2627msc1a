#include <WiFi.h>
#include <esp_system.h>

#include "src/common/ble_reading.h"
#include "src/common/protocol.h"
#include "src/config.h"

#include "src/communication/node_ble.h"
#include "src/communication/node_mqtt.h"
#include "src/communication/node_udp.h"
#include "src/hardware/node_dht.h"
#include "src/hardware/node_display.h"
#include "src/hardware/node_status_led.h"
#include "node_spool.h"

#include <ctime>
#include <string>
#include <variant>

using namespace common::protocol;

// Declared before any function: the Arduino builder inserts auto-generated prototypes
// above the first function definition.
enum class UdpResult { Delivered, Rejected, Failed };

std::string sessionId;
uint32_t sequenceNumber = 0;
bool timeSynchronized = false;
bool sensorError = false;
bool transmissionError = false;

DhtSensor* sensor = nullptr;
StatusLed* statusLed = nullptr;
TemperatureDisplay* display = nullptr;

MqttClient mqtt(
    node::config.node_id,
    node::config.mqtt_host,
    node::config.mqtt_port
);
UdpTransport udp(
    node::config.local_port,
    node::config.server_address,
    node::config.server_port
);

// Status rolling window (one per link, so a reading delivered on one link is not resent on
// it when only the other link was down). Kept in SPIFFS: survives a restart.
Spool mqttSpool("/spool_mqtt.bin", node::spool_capacity);
Spool udpSpool("/spool_udp.bin", node::spool_capacity);

// BLE fallback: while WiFi is down the latest reading is also advertised over BLE.
BleBeacon bleBeacon;

// Readings replayed per loop iteration and link, so a long backlog does not stall the
// sensor loop or flood the MQTT outbox.
constexpr int replay_batch = 5;

// ---- Hardware ------------------------------------------------------------------------------

bool wifiUp() {
    return WiFi.status() == WL_CONNECTED;
}

void updateStatusLed() {
    if (statusLed == nullptr) {
        return;
    }
    statusLed->update(
        wifiUp(),
        sensorError,
        transmissionError,
        timeSynchronized,
        mqtt.isConnected()
    );
}

// Keeps the display button and status LED responsive while waiting.
void serviceHardware() {
    if (display != nullptr) {
        display->update();
    }
    updateStatusLed();
}

void waitServicingHardware(unsigned long durationMs) {
    const unsigned long start = millis();
    while (millis() - start < durationMs) {
        serviceHardware();
        delay(10);
    }
}

void initializeHardware() {
    if (node::config.dht != nullptr) {
        static DhtSensor configuredSensor(*node::config.dht);
        sensor = &configuredSensor;
        sensor->begin();
    }
    if (node::config.status_led != nullptr) {
        static StatusLed configuredStatusLed(*node::config.status_led);
        statusLed = &configuredStatusLed;
        statusLed->begin();
    }
    if (node::config.display != nullptr) {
        static TemperatureDisplay temperatureDisplay(*node::config.display);
        display = &temperatureDisplay;
        display->begin();
    }
}

// ---- Readings ------------------------------------------------------------------------------

/**
 * @brief Obtain the current UTC timestamp using the ESP32 system clock.
 * @return Timestamp formatted as YYYY-MM-DDTHH:MM:SSZ, or an empty string if
 * clock retrieval or formatting fails. Failures are logged to Serial.
 */
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

// Samples the sensor and encodes the next reading. Returns an empty string if no reading
// is available.
std::string buildReading() {

    if (sessionId.empty()) {
        sessionId = std::string("boot-") + std::to_string(esp_random());
        return {};
    }
    if (sensor == nullptr) {
        return {};
    }

    float temperature = 0;
    float humidity = 0;
    if (!sensor->read(temperature, humidity)) {
        sensorError = true;
        if (display != nullptr) {
            display->showUnavailable();
        }
        return {};
    }
    sensorError = false;
    if (display != nullptr) {
        display->showReadings(temperature, humidity);
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

// ---- MQTT (Command Centre) ----------------------------------------------------------------

bool mqttUp() {
    return wifiUp() && mqtt.isConnected();
}

void deliverMqtt(const std::string& datagram) {
    // While a backlog exists the new reading joins it, so readings arrive in order.
    if (mqttUp() && mqttSpool.empty() && mqtt.publishReading(datagram)) {
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
        if (!mqtt.publishReading(datagram)) {
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

    if (!udp.send(datagram)) {
        return UdpResult::Failed;
    }
    if (qos != Qos::AtLeastOnce) {
        return UdpResult::Delivered;
    }

    const unsigned long start = millis();
    while (millis() - start < node::config.response_timeout_ms) {
        switch (udp.receiveResponse(identity.session_id, identity.seq)) {
            case UdpResponse::Ack:
                return UdpResult::Delivered;
            case UdpResponse::Reject:
                return UdpResult::Rejected;   // resending would be rejected again
            default:
                break;
        }
        serviceHardware();
        delay(10);
    }
    Serial.println("[udp] no ACK from alert server");
    return UdpResult::Failed;
}

void deliverUdp(const std::string& datagram) {
    if (udpSpool.empty()) {
        const UdpResult result = udpDeliver(datagram);
        transmissionError = result == UdpResult::Failed;
        if (result != UdpResult::Failed) {
            return;
        }
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
            transmissionError = true;
            break;   // still down: keep it and retry next loop
        }
        transmissionError = false;
        udpSpool.pop();
        Serial.printf("[udp] replayed buffered reading, %u left\n", udpSpool.size());
    }
}

// ---- BLE fallback (alert server) ---------------------------------------------------------

// Advertises the reading while WiFi is down. One-way and best effort: the reading also stays
// in the spools and is replayed over UDP/MQTT later; the server ignores the duplicate.
void deliverBle(const std::string& datagram) {
    if (!node::config.ble_fallback || wifiUp()) {
        return;
    }
    try {
        const Message message = decode(datagram);
        const auto* reading = std::get_if<Reading>(&message);
        if (reading == nullptr) {
            return;
        }
        const auto payload = common::ble::encodeReading(*reading);
        if (!payload) {
            Serial.println("[ble] reading does not fit in an advertisement");
            return;
        }
        bleBeacon.advertise(*payload);
    }
    catch (const std::exception& error) {
        Serial.print("[ble] could not advertise reading: ");
        Serial.println(error.what());
    }
}

// ---- Arduino entry points -----------------------------------------------------------------

/**
 * @brief Initialize Serial, the hardware, the spools, Wi-Fi, UTC time, BLE, UDP and MQTT.
 */
void setup() {

    Serial.begin(node::config.serial_baud);
    initializeHardware();

    // Mount SPIFFS first: readings buffered before a restart are replayed once the links
    // are back. Failure is not fatal; the node then runs without a rolling window.
    if (spoolMount()) {
        mqttSpool.begin();
        udpSpool.begin();
    }

    Serial.println("Connecting to WiFi...");

    WiFi.setAutoReconnect(true);
    WiFi.begin(node::config.wifi_ssid, node::config.wifi_password);

    // Bounded wait: without WiFi the node still runs (spools + BLE fallback) and WiFi keeps
    // reconnecting in the background.
    const unsigned long wifiStart = millis();
    while (!wifiUp() && millis() - wifiStart < node::config.wifi_connect_timeout_ms) {
        updateStatusLed();
        delay(500);
        Serial.print(".");
    }

    Serial.println(wifiUp() ? "\nWiFi connected!" : "\nWiFi not available, continuing without it");
    updateStatusLed();

    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    struct tm timeinfo{};
    timeSynchronized = getLocalTime(&timeinfo, node::config.time_sync_threshold_ms);
    Serial.println(timeSynchronized ? "Time synchronized!" : "Time synchronization failed");

    if (node::config.ble_fallback) {
        bleBeacon.begin();
    }

    Serial.print("ESP32 IP: ");
    Serial.println(WiFi.localIP());

    transmissionError = !udp.begin();

    mqtt.begin();
}

/**
 * @brief Take a reading, deliver it on every link (buffering what cannot be delivered),
 * replay buffered readings, and wait before the next cycle.
 */
void loop() {

    const std::string datagram = buildReading();

    // Booted without WiFi: SNTP syncs in the background once WiFi is up.
    if (!timeSynchronized) {
        struct tm timeinfo{};
        if (getLocalTime(&timeinfo, 0)) {
            timeSynchronized = true;
            Serial.println("Time synchronized!");
        }
    }

    if (wifiUp()) {
        bleBeacon.stop();
    }

    if (!datagram.empty()) {
        // Same JSON to the Command Centre; it accepts the UDP envelope as-is.
        deliverMqtt(datagram);
        deliverUdp(datagram);
        deliverBle(datagram);
    }

    // Send what was buffered during an outage (or before a restart) once a link is back.
    replayMqtt();
    replayUdp();

    if (!mqttSpool.empty() || !udpSpool.empty()) {
        Serial.printf("[spool] pending: mqtt=%u udp=%u\n", mqttSpool.size(), udpSpool.size());
    }

    waitServicingHardware(node::config.reading_delay_ms);
}
