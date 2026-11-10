#pragma once

#include "server_config.h"

#include <atomic>
#include <functional>
#include <string>
#include <thread>

struct mosquitto;

class StatusPublisher {
public:
    using HeartbeatFactory = std::function<std::string()>;

    /**
     * @brief Starts the background MQTT publisher.
     * @param config Broker and publication settings.
     * @param make_heartbeat Callback that creates the current heartbeat JSON.
     */
    StatusPublisher(ServerConfig::MqttSettings config, HeartbeatFactory make_heartbeat);

    /** @brief Publishes offline status, disconnects, and joins the publisher thread. */
    ~StatusPublisher();

    StatusPublisher(const StatusPublisher&) = delete;
    StatusPublisher& operator=(const StatusPublisher&) = delete;

    bool connected() const noexcept { return m_connected; }

private:
    static void on_connect(mosquitto* client, void* self, int rc);
    static void on_disconnect(mosquitto* client, void* self, int rc);

    void run();
    void publish_heartbeat();
    void shutdown_session();

    ServerConfig::MqttSettings m_config;
    HeartbeatFactory m_make_heartbeat;
    std::string m_status_topic;
    std::string m_heartbeat_topic;

    mosquitto* m_client = nullptr;
    std::atomic<bool> m_stop{false};
    std::atomic<bool> m_connected{false};
    bool m_heartbeat_due = false;   // only touched by the publisher thread
    std::thread m_thread;
};
