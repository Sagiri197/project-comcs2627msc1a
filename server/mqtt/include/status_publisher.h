#pragma once

#include "mqtt_publisher.h"
#include "server_stats.h"
#include "server_config.h"

#include <atomic>
#include <cstdint>
#include <chrono>
#include <string>
#include <thread>

class StatusPublisher {
public:
    /**
     * @brief Starts periodic heartbeat publication through the shared MQTT publisher.
     * @param mqtt Shared MQTT transport.
     * @param config MQTT settings used for heartbeat topic and interval.
     * @param udp_port UDP port served by the daemon and included in heartbeats.
     */
    StatusPublisher(IMqttPublisher& mqtt, ServerConfig::MqttSettings config, std::uint16_t udp_port);

    /** @brief Stops and joins the heartbeat thread. */
    ~StatusPublisher();

    StatusPublisher(const StatusPublisher&) = delete;
    StatusPublisher& operator=(const StatusPublisher&) = delete;

    bool connected() const noexcept { return m_mqtt.connected(); }
    void reading_received(const std::string& node_id) { m_stats.reading_received(node_id); }
    void invalid_datagram() noexcept { m_stats.invalid_datagram(); }
    void storage_error() noexcept { m_stats.storage_error(); }

private:
    void run();
    void publish_heartbeat();

    IMqttPublisher& m_mqtt;
    std::uint16_t m_udp_port;
    std::chrono::milliseconds m_heartbeat_interval;
    ServerStats m_stats;
    std::string m_heartbeat_topic;
    std::atomic<bool> m_stop{false};
    std::thread m_thread;
};
