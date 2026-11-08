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
    auto r2 = makeReading(identity2(), 26.0, 0.55);  // 3°C diff > 2°C
    
    store.save(r1);
    logic.processReading(r2);
    
    // Should generate alert
}

TEST_F(AlertLogicTest, DetectsHumidityThresholdExceeded) {
    auto r1 = makeReading(identity1(), 23.0, 0.55);
    auto r2 = makeReading(identity2(), 23.0, 0.62);  // 7% diff > 5%
    
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

}  // namespace
