#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>

struct mosquitto;

// Publishes the alert server's status to the Command Centre's MQTT broker, from its own thread
// (see the "MQTT contract" in command-centre/README.md):
//
//   <topic>/status     "online" (QoS 1, retained) on every (re)connect;
//                      "offline" (QoS 1, retained) on stop, and as Last Will if the process dies
//   <topic>/heartbeat  the JSON returned by make_heartbeat(), every `interval` (QoS 0)
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
        std::chrono::milliseconds interval{5000};
        int keepalive_seconds = 10;   // the broker publishes the Last Will ~1.5x this after a crash
    };

    using HeartbeatFactory = std::function<std::string()>;

    StatusPublisher(Config config, HeartbeatFactory make_heartbeat);
    ~StatusPublisher();   // publishes "offline", disconnects, stops the thread

    StatusPublisher(const StatusPublisher&) = delete;
    StatusPublisher& operator=(const StatusPublisher&) = delete;

    bool connected() const noexcept { return m_connected; }

private:
    static void on_connect(mosquitto* client, void* self, int rc);
    static void on_disconnect(mosquitto* client, void* self, int rc);

    void run();
    void publish_heartbeat();
    void shutdown_session();

    Config m_config;
    HeartbeatFactory m_make_heartbeat;
    std::string m_status_topic;
    std::string m_heartbeat_topic;

    mosquitto* m_client = nullptr;
    std::atomic<bool> m_stop{false};
    std::atomic<bool> m_connected{false};
    bool m_heartbeat_due = false;   // only touched by the publisher thread
    std::thread m_thread;
};
