#pragma once

#include "server_config.h"

#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

struct mosquitto;

class IMqttPublisher {
public:
    virtual ~IMqttPublisher() = default;

    /** @brief Queues an MQTT message for publication; returns false after shutdown starts. */
    virtual bool publish(std::string topic, std::string payload, int qos = 0, bool retain = false) = 0;
    virtual bool connected() const noexcept = 0;
};

class MqttPublisher final : public IMqttPublisher {
public:
    explicit MqttPublisher(ServerConfig::MqttSettings config);
    ~MqttPublisher() override;

    MqttPublisher(const MqttPublisher&) = delete;
    MqttPublisher& operator=(const MqttPublisher&) = delete;

    bool publish(std::string topic, std::string payload, int qos = 0, bool retain = false) override;
    bool connected() const noexcept override { return m_connected; }

private:
    struct PendingMessage {
        std::string topic;
        std::string payload;
        int qos;
        bool retain;
    };

    static void on_connect(mosquitto* client, void* self, int rc);
    static void on_disconnect(mosquitto* client, void* self, int rc);

    void run();
    void publish_pending_messages();
    void shutdown_session();

    ServerConfig::MqttSettings m_config;
    std::string m_status_topic;
    mosquitto* m_client = nullptr;
    std::atomic<bool> m_stop{false};
    std::atomic<bool> m_connected{false};
    std::mutex m_pending_mutex;
    std::deque<PendingMessage> m_pending_messages;
    std::thread m_thread;
};