#pragma once

#include "common/protocol.h"
#include "alert_notifier.h"
#include <chrono>
#include <optional>
#include <memory>

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

    /**
     * @brief Creates alert processing logic backed by a reading store.
     * @param max_comparison_gap Maximum elapsed time between readings that may be compared.
     */
    explicit AlertLogic(
        ReadingStore& store,
        AlertNotifierPtr notifier = nullptr,
        std::chrono::milliseconds max_comparison_gap = std::chrono::minutes(5));

    /**
     * @brief Logs a reading and evaluates it against the previous stored reading.
     * @param reading Reading to process.
     */
    std::optional<ComparisonResult> processReading(
        const common::protocol::Reading& reading);

private:
    ReadingStore& m_store;
    AlertNotifierPtr m_notifier;
    std::chrono::steady_clock::time_point m_last_reading_time;
    std::chrono::milliseconds m_max_comparison_gap;
    
    static constexpr double TEMPERATURE_THRESHOLD = 2.0;
    static constexpr double HUMIDITY_THRESHOLD = 5.0;
    
    static constexpr double TEMPERATURE_MIN = 0.0;
    static constexpr double TEMPERATURE_MAX = 50.0;
    
    static constexpr double HUMIDITY_MIN = 20.0;
    static constexpr double HUMIDITY_MAX = 80.0;

    ComparisonResult compareWithLast(const common::protocol::Reading& current,
                                     const common::protocol::Reading& last,
                                     std::chrono::milliseconds time_diff) const;
    void logReading(const common::protocol::Reading& reading) const;
    
    inline void logComparison(const ComparisonResult& result,
                              const common::protocol::Reading& current,
                              const common::protocol::Reading& last) const;
};
