#include "server_stats.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

using namespace std::chrono_literals;

namespace {

// 2026-11-08T21:40:00Z
const ServerStats::Clock::time_point start = ServerStats::Clock::from_time_t(1794174000);

}  // namespace

TEST(ServerStats, HeartbeatBeforeAnyReading) {
    ServerStats stats(start);

    const auto heartbeat = nlohmann::json::parse(stats.heartbeat_json(8080, start + 125s));

    EXPECT_EQ(heartbeat["status"], "online");
    EXPECT_EQ(heartbeat["dateIssued"], "2026-11-08T21:42:05Z");
    EXPECT_EQ(heartbeat["uptimeSeconds"], 125);
    EXPECT_EQ(heartbeat["udpPort"], 8080);
    EXPECT_EQ(heartbeat["readingsReceived"], 0);
    EXPECT_EQ(heartbeat["invalidDatagrams"], 0);
    EXPECT_EQ(heartbeat["storageErrors"], 0);
    EXPECT_TRUE(heartbeat["lastReading"].is_null());
    EXPECT_TRUE(heartbeat["ble"].is_array());
    EXPECT_TRUE(heartbeat["ble"].empty());
}

TEST(ServerStats, HeartbeatCountsAndLastReading) {
    ServerStats stats(start);
    stats.reading_received("esp32-001", start + 10s);
    stats.reading_received("esp32-002", start + 20s);
    stats.invalid_datagram();
    stats.storage_error();
    stats.storage_error();

    const auto heartbeat = nlohmann::json::parse(stats.heartbeat_json(8080, start + 23s));

    EXPECT_EQ(heartbeat["readingsReceived"], 2);
    EXPECT_EQ(heartbeat["invalidDatagrams"], 1);
    EXPECT_EQ(heartbeat["storageErrors"], 2);
    EXPECT_EQ(heartbeat["lastReading"]["nodeId"], "esp32-002");
    EXPECT_EQ(heartbeat["lastReading"]["secondsAgo"], 3);
}

TEST(ServerStats, HeartbeatListsNodesHeardOverBle) {
    ServerStats stats(start);
    stats.ble_reading_received("esp32-002", start + 10s);
    stats.ble_reading_received("esp32-001", start + 15s);
    stats.ble_reading_received("esp32-002", start + 18s);   // latest BLE reading wins

    const auto heartbeat = nlohmann::json::parse(stats.heartbeat_json(8080, start + 20s));

    ASSERT_EQ(heartbeat["ble"].size(), 2u);
    EXPECT_EQ(heartbeat["ble"][0]["nodeId"], "esp32-001");
    EXPECT_EQ(heartbeat["ble"][0]["secondsAgo"], 5);
    EXPECT_EQ(heartbeat["ble"][1]["nodeId"], "esp32-002");
    EXPECT_EQ(heartbeat["ble"][1]["secondsAgo"], 2);
}
