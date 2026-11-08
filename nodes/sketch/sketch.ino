
#include <WiFi.h>
#include <WiFiUdp.h>
#include <DHT.h>
#include <esp_system.h>
#include <mqtt_client.h>   // ESP-IDF esp-mqtt, bundled with the ESP32 Arduino core

#include "../../common/protocol.h"
#include "../config.h"

#include <string>
#include <ctime>
#include <variant>

using namespace common::protocol;

WiFiUDP udp;
DHT dht(node::config.dht_pin, DHT11);

std::string sessionId;
uint32_t sequenceNumber = 0;
bool timeSynchronized = false;

// ---- MQTT (Command Centre) ------------------------------------------------------------
// Topics and payloads follow command-centre/README.md ("MQTT contract").
const std::string mqttWeatherTopic = std::string("comcs/cell1/") + node::config.node_id + "/weather";
const std::string mqttStatusTopic  = std::string("comcs/cell1/") + node::config.node_id + "/status";
esp_mqtt_client_handle_t mqtt = nullptr;
volatile bool mqttConnected = false;

void onMqttEvent(void*, esp_event_base_t, int32_t eventId, void* eventData) {
    auto event = static_cast<esp_mqtt_event_handle_t>(eventData);
    switch (static_cast<esp_mqtt_event_id_t>(eventId)) {
        case MQTT_EVENT_CONNECTED:
            mqttConnected = true;
            // Retained "online"; the broker replaces it with the retained "offline" will
            // if this node disappears without disconnecting.
            esp_mqtt_client_publish(event->client, mqttStatusTopic.c_str(), "online", 0, 1, 1);
            Serial.println("MQTT connected");
            break;
        case MQTT_EVENT_DISCONNECTED:
            mqttConnected = false;
            Serial.println("MQTT disconnected (reconnecting automatically)");
            break;
        case MQTT_EVENT_PUBLISHED:
            Serial.printf("MQTT PUBACK for msg %d\n", event->msg_id);
            break;
        case MQTT_EVENT_ERROR:
            Serial.println("MQTT error");
            break;
        default:
            break;
    }
}

void startMqtt() {
    esp_mqtt_client_config_t cfg{};
    cfg.broker.address.hostname = node::config.mqtt_host;
    cfg.broker.address.port = node::config.mqtt_port;
    cfg.broker.address.transport = MQTT_TRANSPORT_OVER_TCP;
    cfg.credentials.client_id = node::config.node_id;
    cfg.session.keepalive = 10;   // broker publishes the will ~15 s after the node goes silent
    cfg.session.last_will.topic = mqttStatusTopic.c_str();
    cfg.session.last_will.msg = "offline";
    cfg.session.last_will.qos = 1;
    cfg.session.last_will.retain = 1;

    mqtt = esp_mqtt_client_init(&cfg);
    if (mqtt == nullptr) {
        Serial.println("MQTT client init failed");
        return;
    }
    esp_mqtt_client_register_event(mqtt, static_cast<esp_mqtt_event_id_t>(ESP_EVENT_ANY_ID),
                                   onMqttEvent, nullptr);
    esp_mqtt_client_start(mqtt);   // connects and reconnects in its own task
}

// QoS 1 publish. Enqueued in the client's outbox, so it is sent (and retried until the
// broker's PUBACK) even if the broker is briefly unreachable.
void publishReading(const std::string& json) {
    if (mqtt == nullptr) {
        return;
    }
    const int msgId = esp_mqtt_client_enqueue(mqtt, mqttWeatherTopic.c_str(), json.data(),
                                              static_cast<int>(json.size()), 1, 0, true);
    if (msgId < 0) {
        Serial.println("MQTT publish failed (outbox full?)");
    } else {
        Serial.printf("MQTT queued msg %d on %s%s\n", msgId, mqttWeatherTopic.c_str(),
                      mqttConnected ? "" : " (broker offline, will retry)");
    }
}

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

    sequenceNumber++;

    const float humidity = dht.readHumidity();
    const float temperature = dht.readTemperature();
    const std::string timestamp = currentTimestamp();

    if (isnan(humidity) || isnan(temperature)) {
        Serial.println("Could not read DHT11 sensor");
        sequenceNumber--;
        return;
    }
    if (timestamp.empty()) {
        Serial.println("Could not get synchronized timestamp");
        sequenceNumber--;
        return;
    }

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

        // Serialize using your protocol implementation
        std::string datagram = encode(message);

        Serial.println("\nSending reading:");
        Serial.println(datagram.c_str());

        // Same JSON to the Command Centre; it accepts the UDP envelope as-is.
        publishReading(datagram);

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

    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    struct tm timeinfo{};
    timeSynchronized = getLocalTime(&timeinfo, node::config.time_sync_threshold_ms);
    Serial.println(timeSynchronized ? "Time synchronized!" : "Time synchronization failed");

    dht.begin();

    Serial.print("ESP32 IP: ");
    Serial.println(WiFi.localIP());

    // Initialize UDP socket for sending and receiving
    if (udp.begin(node::config.local_port)) {
        Serial.println("UDP socket initialized!");
    } else {
        Serial.println("UDP initialization failed!");
    }

    startMqtt();

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
