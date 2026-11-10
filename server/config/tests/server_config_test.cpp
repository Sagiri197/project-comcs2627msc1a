#include "server_config.h"

#include <gtest/gtest.h>

#include <chrono>

TEST(ServerConfig, LoadsSampleIniIntoRuntimeSettings) {
    const ServerConfig config = load_server_config("config/tests/alert_server_test.ini");

    EXPECT_EQ(config.database_path, "test-readings.sqlite3");
    EXPECT_EQ(config.udp.bind_address, "127.0.0.1");
    EXPECT_EQ(config.udp.port, 18080);
    EXPECT_TRUE(config.mqtt_enabled);
    EXPECT_EQ(config.mqtt.host, "test-broker.invalid");
    EXPECT_EQ(config.mqtt.port, 1884);
    EXPECT_EQ(config.mqtt.client_id, "alert-server-test");
    EXPECT_EQ(config.mqtt.topic, "comcs/test/alert-server");
    EXPECT_EQ(config.mqtt.heartbeat_interval, std::chrono::seconds(3));
    EXPECT_EQ(config.mqtt.keepalive_seconds, 20);
}
