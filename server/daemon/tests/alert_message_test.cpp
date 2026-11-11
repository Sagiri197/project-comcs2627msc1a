#include "alert_message.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

namespace {

AlertLogic::Alert sample() {
    return {"temperatureDifferential", "medium", "Temperature differential 2.6 °C", 2.6, 2.0,
            {"esp32-001", "boot-1", 42}, {"esp32-002", "esp32-001"}};
}

TEST(AlertMessage, IsSmartDataModelAlert) {
    const auto at = std::chrono::system_clock::time_point{} + std::chrono::seconds(1793630402);
    const auto json = nlohmann::json::parse(alert_json(sample(), at));

    EXPECT_EQ(json["type"], "Alert");
    EXPECT_EQ(json["id"], "urn:ngsi-ld:Alert:cell1:esp32-001:boot-1:42:temperatureDifferential");
    EXPECT_EQ(json["category"], "environment");
    EXPECT_EQ(json["subCategory"], "temperatureDifferential");
    EXPECT_EQ(json["severity"], "medium");
    EXPECT_EQ(json["description"], "Temperature differential 2.6 °C");
    EXPECT_EQ(json["alertSource"], "urn:ngsi-ld:Device:alert-server");
    EXPECT_EQ(json["dateIssued"], "2026-11-02T14:40:02Z");
    EXPECT_DOUBLE_EQ(json["data"]["value"].get<double>(), 2.6);
    EXPECT_EQ(json["data"]["nodes"], nlohmann::json::array({"esp32-002", "esp32-001"}));
    EXPECT_EQ(json["data"]["reading"]["seq"], 42);
}

TEST(AlertMessage, SameReadingGivesSameId) {
    const auto a = nlohmann::json::parse(alert_json(sample()));
    const auto b = nlohmann::json::parse(alert_json(sample()));
    EXPECT_EQ(a["id"], b["id"]);
}

}  // namespace
