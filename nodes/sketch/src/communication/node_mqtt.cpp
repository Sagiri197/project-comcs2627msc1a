#include "node_mqtt.h"

#include <Arduino.h>

MqttClient::MqttClient(const char* nodeId, const char* brokerHost,
                       std::uint16_t brokerPort)
        : m_nodeId(nodeId),
            m_brokerHost(brokerHost),
            m_brokerPort(brokerPort),
            m_weatherTopic(std::string("comcs/cell1/") + nodeId + "/weather"),
            m_statusTopic(std::string("comcs/cell1/") + nodeId + "/status") {}

void MqttClient::begin() {
    esp_mqtt_client_config_t config{};
    config.broker.address.hostname = m_brokerHost;
    config.broker.address.port = m_brokerPort;
    config.broker.address.transport = MQTT_TRANSPORT_OVER_TCP;
    config.credentials.client_id = m_nodeId;
    config.session.keepalive = 10;
    config.session.last_will.topic = m_statusTopic.c_str();
    config.session.last_will.msg = "offline";
    config.session.last_will.qos = 1;
    config.session.last_will.retain = 1;

    m_client = esp_mqtt_client_init(&config);
    if (m_client == nullptr) {
        Serial.println("MQTT client init failed");
        return;
    }
    esp_mqtt_client_register_event(m_client, static_cast<esp_mqtt_event_id_t>(ESP_EVENT_ANY_ID), onEvent, this);
    esp_mqtt_client_start(m_client);
}

bool MqttClient::isConnected() const {
    return m_connected;
}

void MqttClient::publishReading(const std::string& json) {
    
    if (m_client == nullptr)
        return;

    const int messageId = esp_mqtt_client_enqueue(m_client, m_weatherTopic.c_str(), json.data(), static_cast<int>(json.size()), 1, 0, true);
    if (messageId < 0)
        Serial.println("MQTT publish failed (outbox full?)");
    else
        Serial.printf("MQTT queued msg %d on %s%s\n", messageId, m_weatherTopic.c_str(), m_connected ? "" : " (broker offline, will retry)");
}

void MqttClient::onEvent(void* context, esp_event_base_t, std::int32_t eventId, void* eventData) {
    auto& client = *static_cast<MqttClient*>(context);
    auto event = static_cast<esp_mqtt_event_handle_t>(eventData);

    switch (static_cast<esp_mqtt_event_id_t>(eventId)) {
        case MQTT_EVENT_CONNECTED:
            client.m_connected = true;
            esp_mqtt_client_publish(event->client, client.m_statusTopic.c_str(), "online", 0, 1, 1);
            Serial.println("MQTT connected");
            break;
        case MQTT_EVENT_DISCONNECTED:
            client.m_connected = false;
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