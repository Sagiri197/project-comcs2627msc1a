#include "node_mqtt.h"

#include <Arduino.h>
#include <mqtt_client.h>   // ESP-IDF esp-mqtt, bundled with the ESP32 Arduino core

#include "../config.h"

namespace {

const std::string weatherTopic = std::string("comcs/cell1/") + node::config.node_id + "/weather";
const std::string statusTopic  = std::string("comcs/cell1/") + node::config.node_id + "/status";

esp_mqtt_client_handle_t client = nullptr;
volatile bool connected = false;

void onMqttEvent(void*, esp_event_base_t, int32_t eventId, void* eventData) {
    auto event = static_cast<esp_mqtt_event_handle_t>(eventData);
    switch (static_cast<esp_mqtt_event_id_t>(eventId)) {
        case MQTT_EVENT_CONNECTED:
            connected = true;
            // Retained "online"; the broker replaces it with the retained "offline" will
            // if this node disappears without disconnecting.
            esp_mqtt_client_publish(event->client, statusTopic.c_str(), "online", 0, 1, 1);
            Serial.println("MQTT connected");
            break;
        case MQTT_EVENT_DISCONNECTED:
            connected = false;
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

}  // namespace

void mqttStart() {
    esp_mqtt_client_config_t cfg{};
    cfg.broker.address.hostname = node::config.mqtt_host;
    cfg.broker.address.port = node::config.mqtt_port;
    cfg.broker.address.transport = MQTT_TRANSPORT_OVER_TCP;
    cfg.credentials.client_id = node::config.node_id;
    cfg.session.keepalive = 10;   // broker publishes the will ~15 s after the node goes silent
    cfg.session.last_will.topic = statusTopic.c_str();
    cfg.session.last_will.msg = "offline";
    cfg.session.last_will.qos = 1;
    cfg.session.last_will.retain = 1;

    client = esp_mqtt_client_init(&cfg);
    if (client == nullptr) {
        Serial.println("MQTT client init failed");
        return;
    }
    esp_mqtt_client_register_event(client, static_cast<esp_mqtt_event_id_t>(ESP_EVENT_ANY_ID),
                                   onMqttEvent, nullptr);
    esp_mqtt_client_start(client);   // connects and reconnects in its own task
}

void mqttPublishReading(const std::string& json) {
    if (client == nullptr) {
        return;
    }
    const int msgId = esp_mqtt_client_enqueue(client, weatherTopic.c_str(), json.data(),
                                              static_cast<int>(json.size()), 1, 0, true);
    if (msgId < 0) {
        Serial.println("MQTT publish failed (outbox full?)");
    } else {
        Serial.printf("MQTT queued msg %d on %s%s\n", msgId, weatherTopic.c_str(),
                      connected ? "" : " (broker offline, will retry)");
    }
}
