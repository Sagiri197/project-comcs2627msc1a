#include "mqtt_publisher.h"
#include "common/mqtt_topics.h"

#include <mosquitto.h>
#include <spdlog/spdlog.h>

#include <chrono>
#include <stdexcept>
#include <utility>

namespace {

constexpr char online_payload[] = "online";
constexpr char offline_payload[] = "offline";
constexpr auto retry_delay = std::chrono::seconds(2);
constexpr auto connect_timeout = std::chrono::seconds(5);
constexpr int loop_timeout_ms = 100;

int publish_text(mosquitto* client, const std::string& topic, const std::string& payload, int qos, bool retain) {
    return mosquitto_publish(client, nullptr, topic.c_str(), static_cast<int>(payload.size()),
                             payload.data(), qos, retain);
}

}  // namespace

MqttPublisher::MqttPublisher(ServerConfig::MqttSettings config)
    : m_config(std::move(config)),
    m_status_topic(common::mqtt::status_topic(m_config.topic)) {
    
    mosquitto_lib_init();

    m_client = mosquitto_new(m_config.client_id.c_str(), true, this);
    if (m_client == nullptr) {
        mosquitto_lib_cleanup();
        throw std::runtime_error("could not create the MQTT client");
    }
    mosquitto_connect_callback_set(m_client, &MqttPublisher::on_connect);
    mosquitto_disconnect_callback_set(m_client, &MqttPublisher::on_disconnect);

    mosquitto_will_set(m_client, m_status_topic.c_str(), static_cast<int>(sizeof(offline_payload) - 1),
                       offline_payload, 1, true);

    m_thread = std::thread(&MqttPublisher::run, this);
}

MqttPublisher::~MqttPublisher() {
    m_stop = true;
    if (m_thread.joinable()) {
        m_thread.join();
    }
    mosquitto_destroy(m_client);
    mosquitto_lib_cleanup();
}

bool MqttPublisher::publish(std::string topic, std::string payload, int qos, bool retain) {
    std::lock_guard lock(m_pending_mutex);
    if (m_stop)
        return false;

    m_pending_messages.push_back({std::move(topic), std::move(payload), qos, retain});
    return true;
}

void MqttPublisher::on_connect(mosquitto* client, void* self, int rc) {
    auto* publisher = static_cast<MqttPublisher*>(self);
    if (rc != 0) {
        spdlog::warn("MQTT broker {}:{} refused the connection: {}", publisher->m_config.host,
                     publisher->m_config.port, mosquitto_connack_string(rc));
        mosquitto_disconnect(client);
        return;
    }
    publisher->m_connected = true;
    publish_text(client, publisher->m_status_topic, online_payload, 1, true);
    spdlog::info("MQTT connected to {}:{}; status on {}", publisher->m_config.host,
                 publisher->m_config.port, publisher->m_config.topic);
}

void MqttPublisher::on_disconnect(mosquitto*, void* self, int rc) {
    auto* publisher = static_cast<MqttPublisher*>(self);
    const bool was_connected = publisher->m_connected.exchange(false);

    if (was_connected && rc != 0 && !publisher->m_stop) {
        spdlog::warn("MQTT connection to {}:{} lost; reconnecting", publisher->m_config.host,
                     publisher->m_config.port);
    }
}

void MqttPublisher::run() {
    using clock = std::chrono::steady_clock;

    bool session = false;
    bool reported_unreachable = false;
    auto next_attempt = clock::now();
    auto connect_started = clock::now();

    const auto report_unreachable = [&](const char* reason) {
        if (!reported_unreachable) {
            spdlog::warn("MQTT broker {}:{} unreachable ({}); retrying every {} s", m_config.host,
                         m_config.port, reason, retry_delay.count());
            reported_unreachable = true;
        }
    };

    while (!m_stop) {
        const auto now = clock::now();

        if (!session) {
            if (now < next_attempt) {
                std::this_thread::sleep_for(std::chrono::milliseconds(loop_timeout_ms));
                continue;
            }
            const int rc = mosquitto_connect_async(m_client, m_config.host.c_str(), m_config.port,
                                                   m_config.keepalive_seconds);
            if (rc == MOSQ_ERR_SUCCESS) {
                session = true;
                connect_started = now;
            } else {
                report_unreachable(mosquitto_strerror(rc));
                next_attempt = now + retry_delay;
            }
            continue;
        }

        const bool was_connected = m_connected;
        const int rc = mosquitto_loop(m_client, loop_timeout_ms, 1);
        if (rc != MOSQ_ERR_SUCCESS) {
            if (was_connected) {
                reported_unreachable = true;
            } else {
                report_unreachable(mosquitto_strerror(rc));
            }
            m_connected = false;
            session = false;
            next_attempt = clock::now() + retry_delay;
            continue;
        }
        if (!m_connected && clock::now() - connect_started > connect_timeout) {
            report_unreachable("no answer");
            session = false;
            next_attempt = clock::now();
            continue;
        }

        if (m_connected) {
            reported_unreachable = false;
            publish_pending_messages();
        }
    }

    if (m_connected) {
        publish_pending_messages();
    }
    if (session) {
        shutdown_session();
    }
}

void MqttPublisher::publish_pending_messages() {
    std::deque<PendingMessage> messages;
    {
        std::lock_guard lock(m_pending_mutex);
        messages.swap(m_pending_messages);
    }

    for (const auto& message : messages) {
        const int rc = publish_text(m_client, message.topic, message.payload, message.qos, message.retain);
        if (rc != MOSQ_ERR_SUCCESS) {
            spdlog::warn("MQTT message on {} not published: {}", message.topic, mosquitto_strerror(rc));
        }
    }
}

void MqttPublisher::shutdown_session() {
    if (m_connected) {
        publish_text(m_client, m_status_topic, offline_payload, 1, true);
        for (int i = 0; i < 5; ++i) {
            if (mosquitto_loop(m_client, loop_timeout_ms, 1) != MOSQ_ERR_SUCCESS) {
                break;
            }
        }
        spdlog::info("MQTT status set to offline");
    }
    mosquitto_disconnect(m_client);
    mosquitto_loop(m_client, loop_timeout_ms, 1);
    m_connected = false;
}