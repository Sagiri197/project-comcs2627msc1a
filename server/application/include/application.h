#pragma once

#include "common/protocol.h"
#include <map>
#include <optional>
#include <spdlog/spdlog.h>

class AlertLogic 
{
public:
    struct DifferentialResult {
        double temperature_diff;
        double humidity_diff;
        bool exceeds_temp_threshold;
        bool exceeds_humidity_threshold;
        bool out_of_temp_range;
        bool out_of_humidity_range;
    };

    struct AccumulatedReadings {
        std::uint32_t sequence;
        common::protocol::Reading reading1;
        common::protocol::Reading reading2;
        DifferentialResult differential;
    };

    explicit AlertLogic();

    void addReading(const common::protocol::Reading& reading);
    std::optional<AccumulatedReadings> tryProcessSequence(std::uint32_t sequence);
    
    size_t getPendingSequenceCount() const;
    std::optional<AccumulatedReadings> getProcessedReading(std::uint32_t sequence) const;

private:
    struct PendingReading {
        common::protocol::Reading reading1;
        std::optional<common::protocol::Reading> reading2;
    };

    std::map<std::uint32_t, PendingReading> m_pending_sequences;
    std::map<std::uint32_t, AccumulatedReadings> m_processed_sequences;
    
    static constexpr double TEMPERATURE_THRESHOLD = 2.0;
    static constexpr double HUMIDITY_THRESHOLD = 5.0;
    
    static constexpr double TEMPERATURE_MIN = 0.0;
    static constexpr double TEMPERATURE_MAX = 50.0;
    
    static constexpr double HUMIDITY_MIN = 20.0;
    static constexpr double HUMIDITY_MAX = 80.0;

    DifferentialResult calculateDifferential(const common::protocol::Reading& r1, 
                                             const common::protocol::Reading& r2) const;

    void log_accumulated_reading(const AccumulatedReadings& accumulated) const;
};
