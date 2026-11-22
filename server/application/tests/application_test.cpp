#include "application.h"
#include "reading_store.h"
#include <gtest/gtest.h>
#include <thread>

namespace {

using namespace common::protocol;

Identity identity1() { return {"esp32-1", "boot-1", 1}; }
Identity identity2() { return {"esp32-1", "boot-1", 2}; }

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
    
    const auto result = logic.processReading(r1);

    EXPECT_FALSE(result.has_value());
}

TEST_F(AlertLogicTest, ComparesSecondReadingWithFirst) {
    auto r1 = makeReading(identity1(), 23.0, 0.55);
    auto r2 = makeReading(identity2(), 25.0, 0.56);
    
    store.save(r1);
    const auto result = logic.processReading(r2);

    ASSERT_TRUE(result.has_value());
    EXPECT_DOUBLE_EQ(result->temperature_diff, 2.0);
    EXPECT_DOUBLE_EQ(result->humidity_diff, 1.0);
    EXPECT_FALSE(result->exceeds_temp_threshold);
    EXPECT_FALSE(result->exceeds_humidity_threshold);
    EXPECT_FALSE(result->out_of_temp_range);
    EXPECT_FALSE(result->out_of_humidity_range);
}

TEST_F(AlertLogicTest, ComparesReadingsFromSameEspAcrossSessions) {
    const auto previous = makeReading({"esp32-1", "boot-1", 42}, 23.0, 0.55);
    const auto current = makeReading({"esp32-1", "boot-2", 1}, 25.0, 0.56);

    store.save(previous);
    const auto result = logic.processReading(current);

    ASSERT_TRUE(result.has_value());
    EXPECT_DOUBLE_EQ(result->temperature_diff, 2.0);
    EXPECT_DOUBLE_EQ(result->humidity_diff, 1.0);
}

TEST_F(AlertLogicTest, DoesNotCompareAgainstAnotherEsp) {
    const auto other_esp = makeReading({"esp32-2", "boot-1", 1}, 23.0, 0.55);
    const auto current = makeReading(identity2(), 25.0, 0.56);

    store.save(other_esp);
    const auto result = logic.processReading(current);

    EXPECT_FALSE(result.has_value());
}

TEST_F(AlertLogicTest, DetectsTemperatureThresholdExceeded) {
    auto r1 = makeReading(identity1(), 23.0, 0.55);
    auto r2 = makeReading(identity2(), 26.0, 0.55);  // 3°C diff > 2°C
    
    store.save(r1);
    const auto result = logic.processReading(r2);

    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->exceeds_temp_threshold);
    EXPECT_FALSE(result->exceeds_humidity_threshold);
}

TEST_F(AlertLogicTest, DetectsHumidityThresholdExceeded) {
    auto r1 = makeReading(identity1(), 23.0, 0.55);
    auto r2 = makeReading(identity2(), 23.0, 0.62);  // 7% diff > 5%
    
    store.save(r1);
    const auto result = logic.processReading(r2);

    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result->exceeds_temp_threshold);
    EXPECT_TRUE(result->exceeds_humidity_threshold);
}

TEST_F(AlertLogicTest, DetectsOutOfRangeTemperature) {
    auto r1 = makeReading(identity1(), 23.0, 0.55);
    auto r2 = makeReading(identity2(), -5.0, 0.55);  // Below 0°C
    
    store.save(r1);
    const auto result = logic.processReading(r2);

    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->out_of_temp_range);
    EXPECT_FALSE(result->out_of_humidity_range);
}

TEST_F(AlertLogicTest, DetectsOutOfRangeHumidity) {
    auto r1 = makeReading(identity1(), 23.0, 0.55);
    auto r2 = makeReading(identity2(), 23.0, 0.15);  // Below 20%
    
    store.save(r1);
    const auto result = logic.processReading(r2);

    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result->out_of_temp_range);
    EXPECT_TRUE(result->out_of_humidity_range);
}

TEST_F(AlertLogicTest, AllowsNormalReadings) {
    auto r1 = makeReading(identity1(), 23.0, 0.55);
    auto r2 = makeReading(identity2(), 23.5, 0.56);  // 0.5°C, 1% - OK
    
    store.save(r1);
    const auto result = logic.processReading(r2);

    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result->exceeds_temp_threshold);
    EXPECT_FALSE(result->exceeds_humidity_threshold);
    EXPECT_FALSE(result->out_of_temp_range);
    EXPECT_FALSE(result->out_of_humidity_range);
}

TEST(AlertLogicValidityTest, SkipsComparisonWhenReadingGapExceedsConfiguredMaximum) {
    ReadingStore store{":memory:"};
    AlertLogic logic{store, nullptr, std::chrono::milliseconds(1)};
    auto first = makeReading(identity1(), 23.0, 0.55);
    auto second = makeReading(identity2(), 26.0, 0.55);

    EXPECT_FALSE(logic.processReading(first).has_value());
    store.save(first);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    EXPECT_FALSE(logic.processReading(second).has_value());
}

}  // namespace
