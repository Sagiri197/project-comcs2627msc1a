
#include <WiFi.h>
#include <esp_system.h>

#include "src/common/protocol.h"
#include "src/config.h"

#include "src/hardware/node_dht.h"
#include "src/hardware/node_display.h"
#include "src/hardware/node_status_led.h"
#include "src/communication/node_mqtt.h"
#include "src/communication/node_udp.h"

#include <string>
#include <cmath>
#include <ctime>

using namespace common::protocol;

std::string sessionId;
uint32_t sequenceNumber = 0;
bool timeSynchronized = false;
bool sensorError = false;
bool transmissionError = false;

DhtSensor* sensor = nullptr;
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
StatusLed* statusLed = nullptr;
TemperatureDisplay* display = nullptr;

void updateStatusLed() {
    if (statusLed == nullptr) {
        return;
    }
    statusLed->update(
        WiFi.status() == WL_CONNECTED,
        sensorError,
        transmissionError,
        timeSynchronized,
        mqtt.isConnected()
    );
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
        static TemperatureDisplay temperatureDisplay(
            *node::config.display
        );
        display = &temperatureDisplay;
        display->begin();
    }
}

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

/**
 * @brief Sample the sensor and send an encoded reading to MQTT and UDP.
 */
void sendReading() {

    if (sessionId.empty()) {
        sessionId = std::string("boot-") + std::to_string(esp_random());
        return;
    }
    if (sensor == nullptr) {
        return;
    }

    float temperature = 0;
    float humidity = 0;
    if (!sensor->read(temperature, humidity)) {
        sensorError = true;
        if (display != nullptr) {
            display->showUnavailable();
        }
        return;
    }
    sensorError = false;
    if (display != nullptr) {
        display->showReadings(temperature, humidity);
    }

    const std::string timestamp = currentTimestamp();
  
    if (timestamp.empty()) {
        timeSynchronized = false;
        Serial.println("Could not get synchronized timestamp");
        return;
    }
    timeSynchronized = true;

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
        mqtt.publishReading(datagram);

        transmissionError = !udp.send(datagram);
    }
    catch (const ProtocolError& error) {
        transmissionError = true;
        Serial.print("Protocol error: ");
        Serial.println(error.what());
    }
    catch (const std::exception& error) {
        transmissionError = true;
        Serial.print("Error: ");
        Serial.println(error.what());
    }
}

/**
 * @brief Initialize Serial, Wi-Fi, UTC time, the sensor, UDP, and MQTT.
 */
void setup() {

    Serial.begin(node::config.serial_baud);
    initializeHardware();

    Serial.println("Connecting to WiFi...");

    WiFi.begin(node::config.wifi_ssid, node::config.wifi_password);

    while (WiFi.status() != WL_CONNECTED) {
        updateStatusLed();
        delay(500);
        Serial.print(".");
    }

    Serial.println("\nWiFi connected!");
    updateStatusLed();

    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    struct tm timeinfo{};
    timeSynchronized = getLocalTime(&timeinfo, node::config.time_sync_threshold_ms);
    Serial.println(timeSynchronized ? "Time synchronized!" : "Time synchronization failed");

    Serial.print("ESP32 IP: ");
    Serial.println(WiFi.localIP());

    transmissionError = !udp.begin();

    mqtt.begin();
}

/**
 * @brief Send a reading, poll for UDP responses, and wait before the next cycle.
 */
void loop() {

    sendReading();

    // Wait briefly for a response
    unsigned long start = millis();

    while (millis() - start < node::config.response_timeout_ms) {

        udp.receiveResponse(sessionId, sequenceNumber);
        updateStatusLed();
        if (display != nullptr) {
            display->update();
        }

        delay(10);
    }

    start = millis();
    while (millis() - start < node::config.reading_delay_ms) {
        if (display != nullptr) {
            display->update();
        }
        delay(10);
    }
    updateStatusLed();
}
