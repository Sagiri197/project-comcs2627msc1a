#include "server_config.h"

#include <gtest/gtest.h>

#include <chrono>

TEST(ServerConfig, LoadsSampleIniIntoRuntimeSettings) {
    const ServerConfig config = load_server_config("config/alert_server.ini");

    EXPECT_EQ(config.database_path, "readings.sqlite3");
    EXPECT_EQ(config.udp.bind_address, "0.0.0.0");
    EXPECT_EQ(config.udp.port, 8080);
    EXPECT_TRUE(config.mqtt_enabled);
    EXPECT_EQ(config.mqtt.host, "cc.comcs.lan");
    EXPECT_EQ(config.mqtt.port, 1883);
    EXPECT_EQ(config.mqtt.client_id, "alert-server");
    EXPECT_EQ(config.mqtt.topic, "comcs/cell1/alert-server");
    EXPECT_EQ(config.mqtt.heartbeat_interval, std::chrono::seconds(5));
    EXPECT_EQ(config.mqtt.keepalive_seconds, 10);
}
