#pragma once

#include <mqtt_client.h>

#include <cstdint>
#include <string>

class MqttClient {
public:
    MqttClient(const char* nodeId, const char* brokerHost, std::uint16_t brokerPort);

    void begin();
    bool isConnected() const;
    // Queues a reading (QoS 1, retried by the client). False if it could not be queued.
    bool publishReading(const std::string& json);

private:
    static void onEvent(void* context, esp_event_base_t eventBase,
                        std::int32_t eventId, void* eventData);

    const char* m_nodeId;
    const char* m_brokerHost;
    std::uint16_t m_brokerPort;
    const std::string m_weatherTopic;
    const std::string m_statusTopic;
    esp_mqtt_client_handle_t m_client = nullptr;
    volatile bool m_connected = false;
};