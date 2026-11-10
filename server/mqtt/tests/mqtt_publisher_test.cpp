#include "mqtt_publisher.h"

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

using namespace std::chrono_literals;

namespace {

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

TEST(MqttPublisher, BrokerUnreachableDoesNotBlockOrThrow) {
    const auto started = std::chrono::steady_clock::now();
    {
        MqttPublisher publisher(unreachable_broker());
        IMqttPublisher& message_publisher = publisher;
        EXPECT_TRUE(message_publisher.publish("comcs/test/alerts", "{}", 1, false));
        std::this_thread::sleep_for(500ms);
        EXPECT_FALSE(publisher.connected());
    }
    const auto elapsed = std::chrono::steady_clock::now() - started;

    EXPECT_LT(elapsed, 3s);
}

TEST(MqttPublisher, StopsImmediatelyAfterConstruction) {
    const auto started = std::chrono::steady_clock::now();
    { MqttPublisher publisher(unreachable_broker()); }
    EXPECT_LT(std::chrono::steady_clock::now() - started, 2s);
}