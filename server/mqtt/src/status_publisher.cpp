#include "status_publisher.h"

#include <spdlog/spdlog.h>

#include <chrono>

namespace {

constexpr auto status_poll_interval = std::chrono::milliseconds(100);

}  // namespace

StatusPublisher::StatusPublisher(IMqttPublisher& mqtt, ServerConfig::MqttSettings config,
                                 std::uint16_t udp_port)
    : m_mqtt(mqtt),
      m_udp_port(udp_port),
      m_heartbeat_interval(config.heartbeat_interval),
      m_heartbeat_topic(config.topic + "/heartbeat"),
      m_thread(&StatusPublisher::run, this) {}

StatusPublisher::~StatusPublisher() {
    m_stop = true;
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

void StatusPublisher::run() {
    using clock = std::chrono::steady_clock;

    bool was_connected = false;
    auto next_heartbeat = clock::now();
    while (!m_stop) {
        const bool connected = m_mqtt.connected();
        const auto now = clock::now();
        if (connected && (!was_connected || now >= next_heartbeat)) {
            publish_heartbeat();
            next_heartbeat = now + m_heartbeat_interval;
        }
        was_connected = connected;
        std::this_thread::sleep_for(status_poll_interval);
    }
}

void StatusPublisher::publish_heartbeat() {
    const std::string payload = m_stats.heartbeat_json(m_udp_port);
    if (!m_mqtt.publish(m_heartbeat_topic, payload, 0, false)) {
        spdlog::warn("MQTT heartbeat could not be queued");
    }
}