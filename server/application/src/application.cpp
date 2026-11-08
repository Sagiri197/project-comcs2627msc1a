#include "application.h"
#include "reading_store.h"
#include <spdlog/spdlog.h>
#include <cmath>

namespace {
// The protocol carries relativeHumidity as an NGSI-LD fraction (0..1);
// the alert thresholds and logs are expressed in percent.
double toPercent(double fraction) { return fraction * 100.0; }
}  // namespace

AlertLogic::AlertLogic(ReadingStore& store)
    : m_store(store), m_last_reading_time(std::chrono::system_clock::now()) {}

void AlertLogic::processReading(const common::protocol::Reading& reading) {
    const auto now = std::chrono::system_clock::now();
    
    // Always log the received reading
    logReading(reading);
    
    // Try to get last reading from database
    if (auto last = m_store.getLastReading()) {

        auto result = compareWithLast(reading, *last);
        
        spdlog::info("Time difference: {}ms", result.time_diff.count());
        spdlog::info("Temperature differential: {:.2f}°C (threshold: {:.2f}°C)", result.temperature_diff, TEMPERATURE_THRESHOLD);
        spdlog::info("Humidity differential: {:.2f}% (threshold: {:.2f}%)", result.humidity_diff, HUMIDITY_THRESHOLD);
        
        logAlert(result, reading, *last);
    } else {
        spdlog::info("First reading received, no comparison yet");
    }
    
    m_last_reading_time = now;
}

AlertLogic::ComparisonResult AlertLogic::compareWithLast(
    const common::protocol::Reading& current,
    const common::protocol::Reading& last) const {
    
    const auto& cp = current.payload;
    const auto& lp = last.payload;
    const double cur_hum = toPercent(cp.relative_humidity);
    const double last_hum = toPercent(lp.relative_humidity);
    
    double temp_diff = std::abs(cp.temperature - lp.temperature);
    double humidity_diff = std::abs(cur_hum - last_hum);
    
    auto time_diff = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now() - m_last_reading_time
    );
    
    // Check ranges
    bool current_temp_valid = cp.temperature >= TEMPERATURE_MIN && cp.temperature <= TEMPERATURE_MAX;
    bool last_temp_valid = lp.temperature >= TEMPERATURE_MIN && lp.temperature <= TEMPERATURE_MAX;
    bool out_of_temp_range = !current_temp_valid || !last_temp_valid;
    
    bool current_hum_valid = cur_hum >= HUMIDITY_MIN && cur_hum <= HUMIDITY_MAX;
    bool last_hum_valid = last_hum >= HUMIDITY_MIN && last_hum <= HUMIDITY_MAX;
    bool out_of_humidity_range = !current_hum_valid || !last_hum_valid;
    
    return {
        temp_diff,
        humidity_diff,
        time_diff,
        temp_diff > TEMPERATURE_THRESHOLD,
        humidity_diff > HUMIDITY_THRESHOLD,
        out_of_temp_range,
        out_of_humidity_range
    };
}

void AlertLogic::logReading(const common::protocol::Reading& reading) const {
    const auto& payload = reading.payload;
    const auto& identity = reading.identity;
    
    spdlog::info("===== READING RECEIVED =====");
    spdlog::info("Node: {}", identity.node_id);
    spdlog::info("Session: {}", identity.session_id);
    spdlog::info("Sequence: {}", identity.seq);
    spdlog::info("Temperature: {:.2f}°C", payload.temperature);
    spdlog::info("Humidity: {:.2f}%", toPercent(payload.relative_humidity));
    spdlog::info("============================");
}

inline void AlertLogic::logAlert(const ComparisonResult& result,
                                 const common::protocol::Reading& current,
                                 const common::protocol::Reading& last) const {
    bool has_alert = result.exceeds_temp_threshold || 
                     result.exceeds_humidity_threshold || 
                     result.out_of_temp_range || 
                     result.out_of_humidity_range;
    
    if (!has_alert) {
        return;
    }
    
    spdlog::warn("⚠️  ALERT ⚠️");
    spdlog::warn("  Previous: {} ({:.2f}°C, {:.2f}%)", 
                 last.identity.node_id, last.payload.temperature, toPercent(last.payload.relative_humidity));
    spdlog::warn("  Current:  {} ({:.2f}°C, {:.2f}%)", 
                 current.identity.node_id, current.payload.temperature, toPercent(current.payload.relative_humidity));
    
    if (result.exceeds_temp_threshold) {
        spdlog::warn("  Temperature differential {:.2f}°C EXCEEDS threshold {:.2f}°C", 
                     result.temperature_diff, TEMPERATURE_THRESHOLD);
    }
    
    if (result.exceeds_humidity_threshold) {
        spdlog::warn("  Humidity differential {:.2f}% EXCEEDS threshold {:.2f}%", 
                     result.humidity_diff, HUMIDITY_THRESHOLD);
    }
    
    if (result.out_of_temp_range) {
        spdlog::warn("  Temperature OUT OF RANGE [{:.1f}°C - {:.1f}°C]", 
                     TEMPERATURE_MIN, TEMPERATURE_MAX);
    }
    
    if (result.out_of_humidity_range) {
        spdlog::warn("  Humidity OUT OF RANGE [{:.1f}% - {:.1f}%]", 
                     HUMIDITY_MIN, HUMIDITY_MAX);
    }
}
