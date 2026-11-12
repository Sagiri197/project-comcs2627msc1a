#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

struct mosquitto;

// Publishes the alert server's status to the Command Centre's MQTT broker, from its own thread
// (see the "MQTT contract" in command-centre/README.md):
//
//   <topic>/status     "online" (QoS 1, retained) on every (re)connect;
//                      "offline" (QoS 1, retained) on stop, and as Last Will if the process dies
//   <topic>/heartbeat  the JSON returned by make_heartbeat(), every `interval` (QoS 0)
//   <alerts_topic>     each payload passed to publish_alert() (QoS 1, not retained)
//
// The caller is never blocked: connecting and reconnecting happen in the background, so the
// alert server keeps receiving readings while the broker is down or unreachable.
class StatusPublisher {
public:
    struct Config {
        std::string host = "cc.comcs.lan";
        std::uint16_t port = 1883;
        std::string client_id = "alert-server";
        std::string topic = "comcs/cell1/alert-server";
        std::string alerts_topic = "comcs/cell1/alerts";
        std::size_t max_pending_alerts = 256;   // oldest dropped beyond this while offline
        std::chrono::milliseconds interval{5000};
        int keepalive_seconds = 10;   // the broker publishes the Last Will ~1.5x this after a crash
    };

    using HeartbeatFactory = std::function<std::string()>;

    StatusPublisher(Config config, HeartbeatFactory make_heartbeat);
    ~StatusPublisher();   // publishes "offline", disconnects, stops the thread

    StatusPublisher(const StatusPublisher&) = delete;
    StatusPublisher& operator=(const StatusPublisher&) = delete;

    bool connected() const noexcept { return m_connected; }

    // Thread-safe and non-blocking: queues the alert for the publisher thread, which sends it
    // as soon as the broker is connected (alerts raised while it is down are kept, up to
    // max_pending_alerts).
    void publish_alert(std::string payload);
    std::size_t pending_alerts() const;

private:
    static void on_connect(mosquitto* client, void* self, int rc);
    static void on_disconnect(mosquitto* client, void* self, int rc);

    void run();
    void publish_heartbeat();
    void publish_pending_alerts();
    void shutdown_session();

    Config m_config;
    HeartbeatFactory m_make_heartbeat;
    std::string m_status_topic;
    std::string m_heartbeat_topic;

    mosquitto* m_client = nullptr;
    std::atomic<bool> m_stop{false};
    std::atomic<bool> m_connected{false};
    bool m_heartbeat_due = false;   // only touched by the publisher thread
    mutable std::mutex m_alerts_mutex;
    std::deque<std::string> m_pending_alerts;
    std::thread m_thread;
};
