
#include <WiFi.h>
#include <esp_system.h>

#include "../../common/protocol.h"
#include "../config.h"

#include "node_dht.h"
#include "node_mqtt.h"
#include "node_udp.h"

#include "../vendor/GotekLEDC68/TM1651.h"

#include <string>
#include <cmath>
#include <ctime>

using namespace common::protocol;

std::string sessionId;
uint32_t sequenceNumber = 0;
bool timeSynchronized = false;
TM1651* statusDisplay = nullptr;
bool sensorError = false;
bool transmissionError = false;

void setStatusLed(bool red, bool green, bool blue) {
    if (!node::config.status_led_enabled) {
        return;
    }
    const bool inverted = node::config.status_led_common_anode;
    digitalWrite(node::config.status_red_pin, red != inverted ? HIGH : LOW);
    digitalWrite(node::config.status_green_pin, green != inverted ? HIGH : LOW);
    digitalWrite(node::config.status_blue_pin, blue != inverted ? HIGH : LOW);
}

void updateStatusLed() {
    if (WiFi.status() != WL_CONNECTED) {
        setStatusLed(false, false, true);
    } else if (sensorError || transmissionError) {
        setStatusLed(true, false, false);
    } else if (!timeSynchronized || !mqttIsConnected()) {
        setStatusLed(true, true, false);
    } else {
        setStatusLed(false, true, false);
    }
}

void showDisplayUnavailable() {
    if (statusDisplay != nullptr) {
        for (uint8_t position = 0; position < 3; ++position) {
            statusDisplay->displayNum(position, 20);
        }
    }
}

void showTemperature(float temperature) {
    if (statusDisplay == nullptr) {
        return;
    }
    const int value = static_cast<int>(std::round(temperature));
    if (value < -99 || value > 999) {
        showDisplayUnavailable();
        return;
    }
    if (value < 0) {
        statusDisplay->displayNum(0, 20);
        statusDisplay->displayNum(1, -value >= 10 ? (-value / 10) : 16);
        statusDisplay->displayNum(2, -value % 10);
    } else {
        statusDisplay->displayNum(0, value >= 100 ? value / 100 : 16);
        statusDisplay->displayNum(1, value >= 10 ? (value / 10) % 10 : 16);
        statusDisplay->displayNum(2, value % 10);
    }
}

void initializeStatusOutputs() {
    if (node::config.status_led_enabled) {
        pinMode(node::config.status_red_pin, OUTPUT);
        pinMode(node::config.status_green_pin, OUTPUT);
        pinMode(node::config.status_blue_pin, OUTPUT);
        setStatusLed(false, false, true);
    }
    if (node::config.display_enabled) {
        static TM1651 display(node::config.display_clk_pin, node::config.display_dio_pin);
        statusDisplay = &display;
        display.displaySet(node::config.display_brightness > 7 ? 7 : node::config.display_brightness);
        display.displayDP(0);
        showDisplayUnavailable();
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

    float temperature = 0;
    float humidity = 0;
    if (!dhtRead(temperature, humidity)) {
        sensorError = true;
        showDisplayUnavailable();
        return;
    }
    sensorError = false;
    showTemperature(temperature);

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
        mqttPublishReading(datagram);

        transmissionError = !udpSend(datagram);
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
    initializeStatusOutputs();

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

    dhtBegin();

    Serial.print("ESP32 IP: ");
    Serial.println(WiFi.localIP());

    udpBegin();

    mqttStart();
}

/**
 * @brief Send a reading, poll for UDP responses, and wait before the next cycle.
 */
void loop() {

    sendReading();

    // Wait briefly for a response
    unsigned long start = millis();

    while (millis() - start < node::config.response_timeout_ms) {

        udpReceiveResponse(sessionId, sequenceNumber);
        updateStatusLed();

        delay(10);
    }

    delay(node::config.reading_delay_ms);
    updateStatusLed();
}
