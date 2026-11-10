#include "status_publisher.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>

using namespace std::chrono_literals;

namespace {

// A loopback port with nothing listening: connections are refused immediately.
ServerConfig::MqttSettings unreachable_broker() {
    ServerConfig::MqttSettings config{};
    config.host = "127.0.0.1";
    config.port = 1;
    config.client_id = "alert-server-test";
    config.topic = "comcs/cell1/alert-server-test";
    config.heartbeat_interval = 100ms;
    config.keepalive_seconds = 10;
    return config;
}

}  // namespace

// With no broker, the publisher keeps retrying in the background: it must not throw, must not
// report itself connected, never asks for a heartbeat, and stops promptly.
TEST(StatusPublisher, BrokerUnreachableDoesNotBlockOrThrow) {
    std::atomic<int> heartbeats{0};
    const auto started = std::chrono::steady_clock::now();
    {
        StatusPublisher publisher(unreachable_broker(), [&] {
            ++heartbeats;
            return std::string("{}");
        });
        std::this_thread::sleep_for(500ms);
        EXPECT_FALSE(publisher.connected());
    }
    const auto elapsed = std::chrono::steady_clock::now() - started;

    EXPECT_EQ(heartbeats.load(), 0);
    EXPECT_LT(elapsed, 3s);
}

TEST(StatusPublisher, StopsImmediatelyAfterConstruction) {
    const auto started = std::chrono::steady_clock::now();
    { StatusPublisher publisher(unreachable_broker(), [] { return std::string("{}"); }); }
    EXPECT_LT(std::chrono::steady_clock::now() - started, 2s);
}
