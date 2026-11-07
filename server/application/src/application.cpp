#include "application.h"

#include <spdlog/spdlog.h>
#include <cmath>

AlertLogic::AlertLogic() = default;

void AlertLogic::addReading(const common::protocol::Reading& reading) {
    const auto seq = reading.identity.seq;
    const auto node_id = reading.identity.node_id;
    
    auto it = m_pending_sequences.find(seq);
    
    // First reading for this sequence
    if (it == m_pending_sequences.end()) {
        m_pending_sequences[seq].reading1 = reading;
        spdlog::debug("Sequence {}: received reading from {}", seq, node_id);
    
    // Second reading for this sequence
    } else if (!it->second.reading2.has_value()) {
        it->second.reading2 = reading;
        spdlog::debug("Sequence {}: received reading from {}", seq, node_id);
    
    // Already have both readings
    } else {
        spdlog::warn("Sequence {}: ignoring duplicate reading from {}", seq, node_id);
    }
}

std::optional<AlertLogic::AccumulatedReadings> AlertLogic::tryProcessSequence(std::uint32_t sequence) {
    
    auto it = m_pending_sequences.find(sequence);
    if (it == m_pending_sequences.end()) {
        return std::nullopt;
    }
    
    const auto& pending = it->second;
    
    // Need both readings
    if (!pending.reading2.has_value()) {
        spdlog::debug("Sequence {}: waiting for second reading", sequence);
        return std::nullopt;
    }
    
    const auto& r1 = pending.reading1;
    const auto& r2 = pending.reading2.value();
    
    // Calculate differential
    auto differential = calculateDifferential(r1, r2);
    
    AccumulatedReadings accumulated{
        sequence,
        r1,
        r2,
        differential
    };
    
    // Log the accumulated reading
    log_accumulated_reading(accumulated);
    
    const auto& d = accumulated.differential;
    bool has_alert = d.exceeds_temp_threshold || 
                     d.exceeds_humidity_threshold || 
                     d.out_of_temp_range || 
                     d.out_of_humidity_range;
    
    if (has_alert) {
        spdlog::warn("Sequence {}: ALERT", sequence);
        if (d.exceeds_temp_threshold)
            spdlog::warn("  Temperature differential {:.2f}°C EXCEEDS threshold {:.2f}°C", d.temperature_diff, TEMPERATURE_THRESHOLD);
        
        if (d.exceeds_humidity_threshold)
            spdlog::warn("  Humidity differential {:.2f}% EXCEEDS threshold {:.2f}%", d.humidity_diff, HUMIDITY_THRESHOLD);
        
        if (d.out_of_temp_range)
            spdlog::warn("  Temperature OUT OF RANGE [{:.1f}°C - {:.1f}°C]", TEMPERATURE_MIN, TEMPERATURE_MAX);
        
        if (d.out_of_humidity_range)
            spdlog::warn("  Humidity OUT OF RANGE [{:.1f}% - {:.1f}%]", HUMIDITY_MIN, HUMIDITY_MAX);
    }
    
    // Store processed result
    m_processed_sequences[sequence] = accumulated;
    
    // Remove from pending
    m_pending_sequences.erase(it);
    
    return accumulated;
}

size_t AlertLogic::getPendingSequenceCount() const {
    return m_pending_sequences.size();
}

std::optional<AlertLogic::AccumulatedReadings> AlertLogic::getProcessedReading(std::uint32_t sequence) const {
    auto it = m_processed_sequences.find(sequence);
    if (it != m_processed_sequences.end()) {
        return it->second;
    }
    return std::nullopt;
}

AlertLogic::DifferentialResult AlertLogic::calculateDifferential(
    const common::protocol::Reading& r1,
    const common::protocol::Reading& r2) const {
    
    const auto& p1 = r1.payload;
    const auto& p2 = r2.payload;
    
    // Calculate absolute differences
    double temp_diff = std::abs(p1.temperature - p2.temperature);
    double humidity_diff = std::abs(p1.relative_humidity - p2.relative_humidity);
    
    // Check ranges
    bool r1_temp_valid = p1.temperature >= TEMPERATURE_MIN && p1.temperature <= TEMPERATURE_MAX;
    bool r2_temp_valid = p2.temperature >= TEMPERATURE_MIN && p2.temperature <= TEMPERATURE_MAX;
    bool out_of_temp_range = !r1_temp_valid || !r2_temp_valid;
    
    bool r1_hum_valid = p1.relative_humidity >= HUMIDITY_MIN && p1.relative_humidity <= HUMIDITY_MAX;
    bool r2_hum_valid = p2.relative_humidity >= HUMIDITY_MIN && p2.relative_humidity <= HUMIDITY_MAX;
    bool out_of_humidity_range = !r1_hum_valid || !r2_hum_valid;
    
    return {
        temp_diff,
        humidity_diff,
        temp_diff > TEMPERATURE_THRESHOLD,
        humidity_diff > HUMIDITY_THRESHOLD,
        out_of_temp_range,
        out_of_humidity_range
    };
}

void AlertLogic::log_accumulated_reading(const AccumulatedReadings& accumulated) const {
    const auto& d = accumulated.differential;
    const auto& p1 = accumulated.reading1.payload;
    const auto& p2 = accumulated.reading2.payload;
    const auto& n1 = accumulated.reading1.identity.node_id;
    const auto& n2 = accumulated.reading2.identity.node_id;
    
    spdlog::info("Sequence {} was received", accumulated.sequence);
    spdlog::info("[{}] Temperature: {:.2f}°C, Humidity: {:.2f}%", n1, p1.temperature, p1.relative_humidity);
    spdlog::info("[{}] Temperature: {:.2f}°C, Humidity: {:.2f}%", n2, p2.temperature, p2.relative_humidity);
    spdlog::info("Temperature Differential: {:.2f}°C (threshold: {:.2f}°C)", d.temperature_diff, TEMPERATURE_THRESHOLD);
    spdlog::info("Humidity Differential: {:.2f}% (threshold: {:.2f}%)", d.humidity_diff, HUMIDITY_THRESHOLD);
}
