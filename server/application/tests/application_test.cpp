#include "application.h"
#include "reading_store.h"
#include <gtest/gtest.h>

namespace {

using namespace common::protocol;

Identity identity1() { return {"esp32-1", "boot-1", 1}; }
Identity identity2() { return {"esp32-2", "boot-1", 2}; }

Reading makeReading(const Identity& id, double temp, double humidity) {
    return {id, Qos::AtLeastOnce,
            {"urn:ngsi-ld:WeatherObserved:" + id.node_id + ":" + id.session_id + ":" + std::to_string(id.seq), 
             "2026-11-02T14:30:01Z",
             {-8.608, 41.178}, 
             "urn:ngsi-ld:Device:" + id.node_id, 
             temp, 
             humidity}};
}

class AlertLogicTest : public testing::Test {
protected:
    ReadingStore store{":memory:"};
    AlertLogic logic{store};
};

TEST_F(AlertLogicTest, ProcessesFirstReadingWithoutComparison) {
    auto r1 = makeReading(identity1(), 23.0, 0.55);
    
    logic.processReading(r1);
    
    // Should not crash, just log "First reading"
}

TEST_F(AlertLogicTest, ComparesSecondReadingWithFirst) {
    auto r1 = makeReading(identity1(), 23.0, 0.55);
    auto r2 = makeReading(identity2(), 25.0, 0.56);
    
    store.save(r1);
    logic.processReading(r2);
    
    // Should calculate differential: |23-25|=2°C, |55-56|=1%
}

TEST_F(AlertLogicTest, DetectsTemperatureThresholdExceeded) {
    auto r1 = makeReading(identity1(), 23.0, 0.55);
    auto r2 = makeReading(identity2(), 26.0, 0.55);  // 3°C diff > 0.5°C
    
    store.save(r1);
    logic.processReading(r2);
    
    // Should generate alert
}

TEST_F(AlertLogicTest, DetectsHumidityThresholdExceeded) {
    auto r1 = makeReading(identity1(), 23.0, 0.55);
    auto r2 = makeReading(identity2(), 23.0, 0.62);  // 7% diff > 3%
    
    store.save(r1);
    logic.processReading(r2);
    
    // Should generate alert
}

TEST_F(AlertLogicTest, DetectsOutOfRangeTemperature) {
    auto r1 = makeReading(identity1(), 23.0, 0.55);
    auto r2 = makeReading(identity2(), -5.0, 0.55);  // Below 0°C
    
    store.save(r1);
    logic.processReading(r2);
    
    // Should generate alert
}

TEST_F(AlertLogicTest, DetectsOutOfRangeHumidity) {
    auto r1 = makeReading(identity1(), 23.0, 0.55);
    auto r2 = makeReading(identity2(), 23.0, 0.15);  // Below 20%
    
    store.save(r1);
    logic.processReading(r2);
    
    // Should generate alert
}

TEST_F(AlertLogicTest, AllowsNormalReadings) {
    auto r1 = makeReading(identity1(), 23.0, 0.55);
    auto r2 = makeReading(identity2(), 23.5, 0.56);  // 0.5°C, 1% - OK
    
    store.save(r1);
    logic.processReading(r2);
    
    // Should not generate alert
}

class AlertSinkTest : public testing::Test {
protected:
    ReadingStore store{":memory:"};
    std::vector<AlertLogic::Alert> alerts;
    AlertLogic logic{store, [this](const AlertLogic::Alert& alert) { alerts.push_back(alert); }};
};

TEST_F(AlertSinkTest, NormalReadingsRaiseNothing) {
    store.save(makeReading(identity1(), 23.0, 0.55));
    logic.processReading(makeReading(identity2(), 23.5, 0.56));
    EXPECT_TRUE(alerts.empty());
}

TEST_F(AlertSinkTest, TemperatureDifferentialNamesBothNodes) {
    store.save(makeReading(identity1(), 23.0, 0.55));
    logic.processReading(makeReading(identity2(), 26.0, 0.55));

    ASSERT_EQ(alerts.size(), 1u);
    EXPECT_EQ(alerts[0].sub_category, "temperatureDifferential");
    EXPECT_DOUBLE_EQ(alerts[0].value, 3.0);
    EXPECT_EQ(alerts[0].identity.node_id, "esp32-2");
    EXPECT_EQ(alerts[0].identity.seq, 2u);
    EXPECT_EQ(alerts[0].nodes, (std::vector<std::string>{"esp32-1", "esp32-2"}));
}

TEST_F(AlertSinkTest, HumidityDifferentialInPercent) {
    store.save(makeReading(identity1(), 23.0, 0.55));
    logic.processReading(makeReading(identity2(), 23.0, 0.62));

    ASSERT_EQ(alerts.size(), 1u);
    EXPECT_EQ(alerts[0].sub_category, "humidityDifferential");
    EXPECT_NEAR(alerts[0].value, 7.0, 1e-9);
}

TEST_F(AlertSinkTest, OutOfRangeOnFirstReading) {
    logic.processReading(makeReading(identity1(), 55.0, 0.10));

    ASSERT_EQ(alerts.size(), 2u);
    EXPECT_EQ(alerts[0].sub_category, "temperatureOutOfRange");
    EXPECT_DOUBLE_EQ(alerts[0].threshold, 50.0);
    EXPECT_EQ(alerts[1].sub_category, "humidityOutOfRange");
    EXPECT_DOUBLE_EQ(alerts[1].threshold, 20.0);
}

// A bad previous reading was already reported; it must not be reported again.
TEST_F(AlertSinkTest, OutOfRangeOnlyForCurrentReading) {
    store.save(makeReading(identity1(), 23.0, 0.10));
    logic.processReading(makeReading(identity2(), 23.0, 0.25));

    for (const auto& alert : alerts) {
        EXPECT_NE(alert.sub_category, "humidityOutOfRange");
    }
}

TEST(AlertSink, ThrowingSinkDoesNotPropagate) {
    ReadingStore store{":memory:"};
    AlertLogic logic{store, [](const AlertLogic::Alert&) { throw std::runtime_error("down"); }};
    EXPECT_NO_THROW(logic.processReading(makeReading(identity1(), 60.0, 0.5)));
}

}  // namespace
