#pragma once

#include "common/protocol.h"
#include <chrono>
#include <optional>

class ReadingStore;

class AlertLogic 
{
public:
    /** @brief Differences and threshold/range results for a pair of readings. */
    struct ComparisonResult {
        double temperature_diff;
        double humidity_diff;
        std::chrono::milliseconds time_diff;
        
        bool exceeds_temp_threshold;
        bool exceeds_humidity_threshold;
        
        bool out_of_temp_range;
        bool out_of_humidity_range;
    };

    /** @brief Creates alert processing logic backed by a reading store. */
    explicit AlertLogic(ReadingStore& store);

    /**
     * @brief Logs a reading and evaluates it against the previous stored reading.
     * @param reading Reading to process.
     */
    std::optional<ComparisonResult> processReading(
        const common::protocol::Reading& reading);

private:
    ReadingStore& m_store;
    std::chrono::system_clock::time_point m_last_reading_time;
    
    static constexpr double TEMPERATURE_THRESHOLD = 2.0;
    static constexpr double HUMIDITY_THRESHOLD = 5.0;
    
    static constexpr double TEMPERATURE_MIN = 0.0;
    static constexpr double TEMPERATURE_MAX = 50.0;
    
    static constexpr double HUMIDITY_MIN = 20.0;
    static constexpr double HUMIDITY_MAX = 80.0;

    ComparisonResult compareWithLast(const common::protocol::Reading& current,
                                     const common::protocol::Reading& last) const;
    void logReading(const common::protocol::Reading& reading) const;
    
    inline void logComparison(const ComparisonResult& result,
                              const common::protocol::Reading& current,
                              const common::protocol::Reading& last) const;
};
