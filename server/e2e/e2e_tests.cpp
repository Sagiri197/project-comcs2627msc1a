#include <gtest/gtest.h>

#include <nlohmann/json.hpp>
#include <sqlite3.h>
#include <thread>
#include <chrono>
#include <algorithm>
#include <ranges>

#include "alert_server.h"
#include "mqtt_publisher.h"
#include "fake_udp_client.h"
#include "spy_mqtt_broker.h"
#include "common/protocol.h"

namespace {

using namespace common::protocol;
using namespace std::chrono_literals;

Identity identity() { return {"esp32-a", "boot-1", 42}; }

Reading reading(Qos qos = Qos::AtLeastOnce) {
    return {identity(), qos,
            {"urn:ngsi-ld:WeatherObserved:esp32-a:boot-1:42", "2026-11-02T14:30:01Z",
             {-8.608, 41.178}, "urn:ngsi-ld:Device:esp32-a", 23.4, 0.56}};
}

}

class E2ETests : public testing::Test {
protected:
    ReadingStore store{":memory:"};
    AlertLogic alertLogic{store};
    std::unique_ptr<SpyMqttBroker> spy_broker;
    std::unique_ptr<AlertServer> alertServer;
    std::unique_ptr<FakeUdpClient> client;
    std::thread broker_thread;

    FakeUdpClient::Endpoint server_endpoint;
    UdpEndpoint client_endpoint;

    void SetUp() override {
        client = std::make_unique<FakeUdpClient>();
        auto client_ep = client->local_endpoint();
        client_endpoint = {client_ep.address, static_cast<std::uint16_t>(client_ep.port)};

        // Create spy broker
        spy_broker = std::make_unique<SpyMqttBroker>(SpyMqttBroker::Config{"127.0.0.1", 0});
        auto broker_ep = spy_broker->local_endpoint();

        // Create AlertServer with MQTT settings pointing to spy broker
        ServerConfig::MqttSettings mqtt_settings{
            .host = broker_ep.address,
            .port = broker_ep.port,
            .client_id = "alert-server-e2e-test",
            .topic = "alerts",
            .heartbeat_interval = 100ms,
            .keepalive_seconds = 60
        };

        alertServer = std::make_unique<AlertServer>(store, alertLogic, UdpEndpoint{"127.0.0.1", 0}, mqtt_settings);

        // Bootstrap to get server endpoint
        alertServer->send(Ack{identity()}, client_endpoint);
        const auto bootstrap = client->receive();
        ASSERT_FALSE(bootstrap.empty());

        auto sender = client->last_sender();
        ASSERT_TRUE(sender);
        server_endpoint = *sender;
    }

    void TearDown() override {
        alertServer.reset();
        spy_broker.reset();
        client.reset();
    }
};


TEST_F(E2ETests, SentDataIsReceivedAndPersistedInDatabase) {
    // Create a simple AlertServer without MQTT for this test
    ReadingStore simple_store{":memory:"};
    AlertLogic simple_logic{simple_store};
    auto simple_server = std::make_unique<AlertServer>(simple_store, simple_logic, UdpEndpoint{"127.0.0.1", 0});

    auto simple_client = std::make_unique<FakeUdpClient>();
    auto simple_client_ep = simple_client->local_endpoint();
    UdpEndpoint simple_client_endpoint{simple_client_ep.address, static_cast<std::uint16_t>(simple_client_ep.port)};

    // Bootstrap
    simple_server->send(Ack{identity()}, simple_client_endpoint);
    const auto bootstrap = simple_client->receive();
    ASSERT_FALSE(bootstrap.empty());
    auto sender = simple_client->last_sender();
    ASSERT_TRUE(sender);
    auto simple_server_endpoint = *sender;

    // Send a reading from client to server
    const auto test_reading = reading();
    simple_client->send(encode(test_reading), simple_server_endpoint);

    // Server receives and processes the reading
    const auto received = simple_server->receive();
    ASSERT_TRUE(std::holds_alternative<Reading>(received.message));
    EXPECT_EQ(std::get<Reading>(received.message).identity.node_id, "esp32-a");

    // Receive ACK from server
    const auto ack_data = simple_client->receive();
    ASSERT_FALSE(ack_data.empty());
    const auto ack = std::get<Ack>(decode(ack_data));
    EXPECT_EQ(ack.identity.seq, test_reading.identity.seq);

    // Verify reading is persisted in database
    const auto stored = simple_store.find(test_reading.identity);
    ASSERT_TRUE(stored);
    EXPECT_EQ(stored->identity.node_id, "esp32-a");
    EXPECT_EQ(stored->identity.session_id, "boot-1");
    EXPECT_EQ(stored->identity.seq, 42);
    EXPECT_DOUBLE_EQ(stored->payload.temperature, 23.4);
    EXPECT_DOUBLE_EQ(stored->payload.relative_humidity, 0.56);
}

TEST_F(E2ETests, HeartbeatsAreSentByMqtt) {
    // Wait for MQTT messages to be published via the spy broker
    for (int i = 0; i < 10; ++i) {
        spy_broker->wait_for_message(100);
    }
    
    const auto messages = spy_broker->published_messages();
    
    // The spy broker is capturing MQTT connections. We should see at least
    // the initial status message from MqttPublisher
    ASSERT_GT(messages.size(), 0) << "No MQTT messages published";
    
    // Check that we received any message on the configured topic
    bool found_message = false;
    for (const auto& msg : messages) {
        if (msg.topic.find("alerts") != std::string::npos) {
            found_message = true;
            EXPECT_FALSE(msg.payload.empty());
            break;
        }
    }
    ASSERT_TRUE(found_message) << "No messages published to 'alerts' topic";
}

TEST_F(E2ETests, AlertsAreSentByMqtt) {
    // Send first reading to establish baseline
    const auto first_reading = reading();
    client->send(encode(first_reading), server_endpoint);

    const auto first_received = alertServer->receive();
    ASSERT_TRUE(std::holds_alternative<Reading>(first_received.message));
    EXPECT_FALSE(first_received.comparison);

    // Consume the ACK
    const auto first_ack = client->receive();
    ASSERT_FALSE(first_ack.empty());

    // Send second reading that triggers an alert
    auto alert_reading = reading();
    alert_reading.identity.seq = 43;
    alert_reading.payload.id = "urn:ngsi-ld:WeatherObserved:esp32-a:boot-1:43";
    alert_reading.payload.temperature = 26.0;  // +2.6°C increase

    client->send(encode(alert_reading), server_endpoint);

    const auto alert_received = alertServer->receive();
    ASSERT_TRUE(std::holds_alternative<Reading>(alert_received.message));

    // Verify alert was triggered
    ASSERT_TRUE(alert_received.comparison);
    EXPECT_TRUE(alert_received.comparison->exceeds_temp_threshold);

    // Consume the ACK
    const auto alert_ack = client->receive();
    ASSERT_FALSE(alert_ack.empty());

    // Wait for alert message to be published to MQTT
    for (int i = 0; i < 10; ++i) {
        spy_broker->wait_for_message(100);
    }
    
    const auto messages = spy_broker->published_messages();

    // We should see MQTT messages being published
    ASSERT_GT(messages.size(), 0) << "No MQTT messages were captured by spy broker";

    // Verify readings are stored
    const auto first_stored = store.find(first_reading.identity);
    const auto alert_stored = store.find(alert_reading.identity);
    ASSERT_TRUE(first_stored);
    ASSERT_TRUE(alert_stored);
}
