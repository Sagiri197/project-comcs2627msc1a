#include "application.h"
#include <gtest/gtest.h>
#include <cmath>

namespace {

using namespace common::protocol;

Identity identity1() { return {"esp32-1", "boot-1", 42}; }
Identity identity2() { return {"esp32-2", "boot-1", 42}; }

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
    AlertLogic logic;
};

TEST_F(AlertLogicTest, AccumulatesReadingsBySequence) {
    auto r1 = makeReading(identity1(), 23.0, 55.0);
    auto r2 = makeReading(identity2(), 24.0, 60.0);
    
    logic.addReading(r1);
    EXPECT_EQ(logic.getPendingSequenceCount(), 1);
    
    // Not enough readings yet (need both devices)
    auto result1 = logic.tryProcessSequence(42);
    EXPECT_FALSE(result1.has_value());
    
    // Add second device
    logic.addReading(r2);
    EXPECT_EQ(logic.getPendingSequenceCount(), 1);
    
    // Now we should have both
    auto result2 = logic.tryProcessSequence(42);
    EXPECT_TRUE(result2.has_value());
    EXPECT_EQ(result2->sequence, 42);
}

TEST_F(AlertLogicTest, CalculatesTemperatureDifferential) {
    auto r1 = makeReading(identity1(), 23.0, 55.0);
    auto r2 = makeReading(identity2(), 25.0, 55.0);
    
    logic.addReading(r1);
    logic.addReading(r2);
    
    auto result = logic.tryProcessSequence(42);
    ASSERT_TRUE(result.has_value());
    
    // |23.0 - 25.0| = 2.0°C
    EXPECT_DOUBLE_EQ(result->differential.temperature_diff, 2.0);
}

TEST_F(AlertLogicTest, DetectsExceededTemperatureThreshold) {
    auto r1 = makeReading(identity1(), 23.0, 55.0);
    auto r2 = makeReading(identity2(), 26.0, 55.0);  // Diff = 3.0°C > 2.0°C
    
    logic.addReading(r1);
    logic.addReading(r2);
    
    auto result = logic.tryProcessSequence(42);
    ASSERT_TRUE(result.has_value());
    
    EXPECT_GT(result->differential.temperature_diff, 2.0);
    EXPECT_TRUE(result->differential.exceeds_temp_threshold);
}

TEST_F(AlertLogicTest, DetectsExceededHumidityThreshold) {
    auto r1 = makeReading(identity1(), 23.0, 55.0);
    auto r2 = makeReading(identity2(), 23.0, 62.0);  // Diff = 7.0% > 5.0%
    
    logic.addReading(r1);
    logic.addReading(r2);
    
    auto result = logic.tryProcessSequence(42);
    ASSERT_TRUE(result.has_value());
    
    EXPECT_GT(result->differential.humidity_diff, 5.0);
    EXPECT_TRUE(result->differential.exceeds_humidity_threshold);
}

TEST_F(AlertLogicTest, DetectsOutOfRangeTemperature) {
    auto r1 = makeReading(identity1(), -5.0, 55.0);  // Below min (0°C)
    auto r2 = makeReading(identity2(), 23.0, 55.0);
    
    logic.addReading(r1);
    logic.addReading(r2);
    
    auto result = logic.tryProcessSequence(42);
    ASSERT_TRUE(result.has_value());
    
    EXPECT_TRUE(result->differential.out_of_temp_range);
}

TEST_F(AlertLogicTest, DetectsOutOfRangeHumidity) {
    auto r1 = makeReading(identity1(), 23.0, 15.0);  // Below min (20%)
    auto r2 = makeReading(identity2(), 23.0, 55.0);
    
    logic.addReading(r1);
    logic.addReading(r2);
    
    auto result = logic.tryProcessSequence(42);
    ASSERT_TRUE(result.has_value());
    
    EXPECT_TRUE(result->differential.out_of_humidity_range);
}

TEST_F(AlertLogicTest, AllowsNormalReadings) {
    auto r1 = makeReading(identity1(), 23.0, 55.0);
    auto r2 = makeReading(identity2(), 23.5, 56.0);  // Small diff, within range
    
    logic.addReading(r1);
    logic.addReading(r2);
    
    auto result = logic.tryProcessSequence(42);
    ASSERT_TRUE(result.has_value());
    
    EXPECT_FALSE(result->differential.exceeds_temp_threshold);
    EXPECT_FALSE(result->differential.exceeds_humidity_threshold);
    EXPECT_FALSE(result->differential.out_of_temp_range);
    EXPECT_FALSE(result->differential.out_of_humidity_range);
}

TEST_F(AlertLogicTest, HandlesMultipleSequences) {
    // Sequence 1: devices 1 and 2
    logic.addReading(makeReading(identity1(), 23.0, 55.0));
    logic.addReading(makeReading(identity2(), 24.0, 56.0));
    
    // Sequence 2: devices 1 and 2
    Identity id1_seq2{"esp32-1", "boot-1", 43};
    Identity id2_seq2{"esp32-2", "boot-1", 43};
    logic.addReading(makeReading(id1_seq2, 25.0, 57.0));
    logic.addReading(makeReading(id2_seq2, 26.0, 58.0));
    
    EXPECT_EQ(logic.getPendingSequenceCount(), 2);
    
    auto seq42 = logic.tryProcessSequence(42);
    EXPECT_TRUE(seq42.has_value());
    EXPECT_EQ(logic.getPendingSequenceCount(), 1);
    
    auto seq43 = logic.tryProcessSequence(43);
    EXPECT_TRUE(seq43.has_value());
    EXPECT_EQ(logic.getPendingSequenceCount(), 0);
}

TEST_F(AlertLogicTest, StoresProcessedSequences) {
    auto r1 = makeReading(identity1(), 23.0, 55.0);
    auto r2 = makeReading(identity2(), 24.0, 56.0);
    
    logic.addReading(r1);
    logic.addReading(r2);
    logic.tryProcessSequence(42);
    
    auto retrieved = logic.getProcessedReading(42);
    EXPECT_TRUE(retrieved.has_value());
    EXPECT_EQ(retrieved->sequence, 42);
}

TEST_F(AlertLogicTest, ReturnsNulloptForNonexistentSequence) {
    auto result = logic.tryProcessSequence(999);
    EXPECT_FALSE(result.has_value());
    
    auto retrieved = logic.getProcessedReading(999);
    EXPECT_FALSE(retrieved.has_value());
}

TEST_F(AlertLogicTest, IgnoresDuplicateReadings) {
    auto r1 = makeReading(identity1(), 23.0, 55.0);
    auto r2 = makeReading(identity2(), 24.0, 60.0);
    auto r1_dup = makeReading(identity1(), 23.5, 55.5);
    
    logic.addReading(r1);
    logic.addReading(r2);
    logic.addReading(r1_dup);  // Duplicate from esp32-1
    
    auto result = logic.tryProcessSequence(42);
    ASSERT_TRUE(result.has_value());
    
    // Should use first reading from esp32-1, not duplicate
    EXPECT_DOUBLE_EQ(result->reading1.payload.temperature, 23.0);
}

}  // namespace
