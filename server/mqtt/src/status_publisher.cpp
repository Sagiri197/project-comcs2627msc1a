#include "status_publisher.h"

#include <mosquitto.h>
#include <spdlog/spdlog.h>

#include <stdexcept>
#include <utility>

namespace {

constexpr char online_payload[] = "online";
constexpr char offline_payload[] = "offline";
constexpr auto retry_delay = std::chrono::seconds(2);
// A connect that has not completed by then is abandoned and retried. Needed on Windows, where
// a refused non-blocking connect is only signalled through select()'s except set, which
// libmosquitto does not watch, so mosquitto_loop() would otherwise wait forever.
constexpr auto connect_timeout = std::chrono::seconds(5);
constexpr int loop_timeout_ms = 100;

int publish_text(mosquitto* client, const std::string& topic, const std::string& payload, int qos, bool retain) {
    return mosquitto_publish(client, nullptr, topic.c_str(), static_cast<int>(payload.size()),
                             payload.data(), qos, retain);
}

}  // namespace

StatusPublisher::StatusPublisher(Config config, HeartbeatFactory make_heartbeat)
    : m_config(std::move(config)),
      m_make_heartbeat(std::move(make_heartbeat)),
      m_status_topic(m_config.topic + "/status"),
      m_heartbeat_topic(m_config.topic + "/heartbeat") {
    mosquitto_lib_init();

    m_client = mosquitto_new(m_config.client_id.c_str(), true, this);
    if (m_client == nullptr) {
        mosquitto_lib_cleanup();
        throw std::runtime_error("could not create the MQTT client");
    }
    mosquitto_connect_callback_set(m_client, &StatusPublisher::on_connect);
    mosquitto_disconnect_callback_set(m_client, &StatusPublisher::on_disconnect);

    // If the alert server dies without disconnecting, the broker marks it offline.
    mosquitto_will_set(m_client, m_status_topic.c_str(), static_cast<int>(sizeof(offline_payload) - 1),
                       offline_payload, 1, true);

    // Every mosquitto call below happens on this one thread (the Windows build of libmosquitto
    // has no internal locking).
    m_thread = std::thread(&StatusPublisher::run, this);
}

StatusPublisher::~StatusPublisher() {
    m_stop = true;
    if (m_thread.joinable()) {
        m_thread.join();
    }
    mosquitto_destroy(m_client);
    mosquitto_lib_cleanup();
}

void StatusPublisher::on_connect(mosquitto* client, void* self, int rc) {
    auto* publisher = static_cast<StatusPublisher*>(self);
    if (rc != 0) {
        spdlog::warn("MQTT broker {}:{} refused the connection: {}", publisher->m_config.host,
                     publisher->m_config.port, mosquitto_connack_string(rc));
        mosquitto_disconnect(client);
        return;
    }
    publisher->m_connected = true;
    publisher->m_heartbeat_due = true;   // announce ourselves right away, then every interval
    publish_text(client, publisher->m_status_topic, online_payload, 1, true);
    spdlog::info("MQTT connected to {}:{}; status on {}, heartbeat every {} ms", publisher->m_config.host,
                 publisher->m_config.port, publisher->m_config.topic, publisher->m_config.interval.count());
}

void StatusPublisher::on_disconnect(mosquitto*, void* self, int rc) {
    auto* publisher = static_cast<StatusPublisher*>(self);
    const bool was_connected = publisher->m_connected.exchange(false);
    if (was_connected && rc != 0 && !publisher->m_stop) {
        spdlog::warn("MQTT connection to {}:{} lost; reconnecting", publisher->m_config.host,
                     publisher->m_config.port);
    }
}

void StatusPublisher::run() {
    using clock = std::chrono::steady_clock;

    bool session = false;            // a socket exists (the CONNACK may still be pending)
    bool reported_unreachable = false;
    auto next_attempt = clock::now();
    auto next_heartbeat = clock::now();
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
            // Non-blocking connect: the TCP handshake and CONNACK complete in mosquitto_loop().
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
                reported_unreachable = true;   // on_disconnect already said "lost"
            } else {
                report_unreachable(mosquitto_strerror(rc));
            }
            m_connected = false;
            session = false;
            next_attempt = clock::now() + retry_delay;
            continue;
        }
        if (!m_connected && clock::now() - connect_started > connect_timeout) {
            // The next mosquitto_connect_async() closes this half-open socket.
            report_unreachable("no answer");
            session = false;
            next_attempt = clock::now();
            continue;
        }

        if (m_connected) {
            reported_unreachable = false;
            if (m_heartbeat_due || clock::now() >= next_heartbeat) {
                publish_heartbeat();
                m_heartbeat_due = false;
                next_heartbeat = clock::now() + m_config.interval;
            }
        }
    }

    if (session) {
        shutdown_session();
    }
}

void StatusPublisher::publish_heartbeat() {
    std::string payload;
    try {
        payload = m_make_heartbeat();
    } catch (const std::exception& error) {
        spdlog::warn("Could not build the MQTT heartbeat: {}", error.what());
        return;
    }
    const int rc = publish_text(m_client, m_heartbeat_topic, payload, 0, false);
    if (rc != MOSQ_ERR_SUCCESS) {
        spdlog::warn("MQTT heartbeat not published: {}", mosquitto_strerror(rc));
    }
}

// A clean disconnect suppresses the Last Will, so say "offline" explicitly first.
void StatusPublisher::shutdown_session() {
    if (m_connected) {
        publish_text(m_client, m_status_topic, offline_payload, 1, true);
        // Give the PUBLISH/PUBACK a moment to go through before closing the socket.
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
