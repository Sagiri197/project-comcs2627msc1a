#include "mqtt_publisher.h"
#include "alert_publisher.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <thread>
#include <utility>

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

class CapturingPublisher : public IMqttPublisher {
public:
    bool publish(std::string topic, std::string payload, int qos, bool retain) override {
        published_topic = std::move(topic);
        published_payload = std::move(payload);
        published_qos = qos;
        published_retain = retain;
        return accept_messages;
    }

    bool connected() const noexcept override { return true; }

    std::string published_topic;
    std::string published_payload;
    int published_qos = -1;
    bool published_retain = true;
    bool accept_messages = true;
};

common::protocol::Reading sample_reading() {
    return {{"esp32-003", "session-1", 42}, common::protocol::Qos::AtLeastOnce,
            {"reading-42", "2026-11-02T14:30:01Z", {-8.608, 41.178}, "device-3", 23.4, 0.56}};
}

}  // namespace

TEST(AlertPublisher, PublishesTriggeredComparisonAsNonRetainedQosOneJson) {
    CapturingPublisher mqtt;
    ServerConfig::MqttSettings config{};
    config.topic = "comcs/cell1/alert-server";
    AlertPublisher alerts(mqtt, config);
    const AlertLogic::ComparisonResult comparison{
        3.2, 1.0, 250ms, true, false, false, false};

    ASSERT_TRUE(alerts.publish(sample_reading(), comparison));

    EXPECT_EQ(mqtt.published_topic, "comcs/cell1/alert-server/alert");
    EXPECT_EQ(mqtt.published_qos, 1);
    EXPECT_FALSE(mqtt.published_retain);
    const auto payload = nlohmann::json::parse(mqtt.published_payload);
    EXPECT_EQ(payload.at("nodeId"), "esp32-003");
    EXPECT_EQ(payload.at("sessionId"), "session-1");
    EXPECT_EQ(payload.at("seq"), 42);
    EXPECT_DOUBLE_EQ(payload.at("temperatureDiff"), 3.2);
    EXPECT_TRUE(payload.at("exceedsTemperatureThreshold").get<bool>());
}

TEST(AlertPublisher, DoesNotPublishWhenNoAlertConditionIsSet) {
    CapturingPublisher mqtt;
    ServerConfig::MqttSettings config{};
    AlertPublisher alerts(mqtt, config);
    const AlertLogic::ComparisonResult comparison{
        1.0, 1.0, 250ms, false, false, false, false};

    EXPECT_FALSE(alerts.publish(sample_reading(), comparison));
    EXPECT_TRUE(mqtt.published_topic.empty());
}

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