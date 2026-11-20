#include "server_config.h"

#include <gtest/gtest.h>

#include <chrono>
#include <fstream>
#include <stdexcept>
#include <string>

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

namespace {

std::string write_ini(const std::string& name, const std::string& ble_section) {
    const std::string path = testing::TempDir() + name;
    std::ofstream out(path);
    out << "[database]\npath = test.sqlite3\n"
           "[udp]\nbind_address = 127.0.0.1\nport = 18080\n"
           "[mqtt]\nenabled = false\nhost = h\nport = 1883\nclient_id = c\ntopic = t\n"
           "heartbeat_interval_seconds = 5\nkeepalive_seconds = 10\n"
           "[email]\nenabled = false\nwebhook_url =\nrecipient_email =\n"
        << ble_section;
    return path;
}

}  // namespace

TEST(ServerConfig, BleIsDisabledWhenSectionIsMissing) {
    const ServerConfig config = load_server_config(write_ini("no_ble.ini", ""));
    EXPECT_FALSE(config.ble.enabled);
    EXPECT_TRUE(config.ble.adapter.empty());
}

TEST(ServerConfig, LoadsBleSettings) {
    const ServerConfig config =
        load_server_config(write_ini("ble.ini", "[ble]\nenabled = true\nadapter = hci0\n"));
    EXPECT_TRUE(config.ble.enabled);
    EXPECT_EQ(config.ble.adapter, "hci0");

    EXPECT_FALSE(load_server_config(write_ini("ble_off.ini", "[ble]\nenabled = false\n")).ble.enabled);
}

TEST(ServerConfig, RejectsInvalidBleFlag) {
    EXPECT_THROW(load_server_config(write_ini("ble_bad.ini", "[ble]\nenabled = yes\n")),
                 std::invalid_argument);
}
