#pragma once

#include "common/protocol.h"
#include <chrono>
#include <functional>
#include <string>
#include <vector>

class ReadingStore;

class AlertLogic 
{
public:
    struct ComparisonResult {
        double temperature_diff;
        double humidity_diff;
        std::chrono::milliseconds time_diff;
        
        bool exceeds_temp_threshold;
        bool exceeds_humidity_threshold;
        
        bool out_of_temp_range;
        bool out_of_humidity_range;
    };

    // One alarm condition raised by a reading. The daemon turns it into a Smart Data Model
    // "Alert" and publishes it on comcs/cell1/alerts (see command-centre/README.md).
    struct Alert {
        std::string sub_category;   // temperatureDifferential, humidityDifferential,
                                    // temperatureOutOfRange, humidityOutOfRange
        std::string severity;       // "medium" for differentials, "high" for out of range
        std::string description;
        double value;               // the differential, or the out-of-range measurement
        double threshold;           // the differential limit, or the violated range bound
        common::protocol::Identity identity;   // the reading that raised the alert
        std::vector<std::string> nodes;        // nodes involved
    };

    using AlertSink = std::function<void(const Alert&)>;

    // `on_alert`, if set, is called once per raised condition, from processReading().
    explicit AlertLogic(ReadingStore& store, AlertSink on_alert = {});

    void processReading(const common::protocol::Reading& reading);

private:
    ReadingStore& m_store;
    AlertSink m_on_alert;
    std::chrono::system_clock::time_point m_last_reading_time;
    
    static constexpr double TEMPERATURE_THRESHOLD = 0.5;
    static constexpr double HUMIDITY_THRESHOLD = 3.0;
    
    static constexpr double TEMPERATURE_MIN = 0.0;
    static constexpr double TEMPERATURE_MAX = 50.0;
    
    static constexpr double HUMIDITY_MIN = 20.0;
    static constexpr double HUMIDITY_MAX = 80.0;

    ComparisonResult compareWithLast(const common::protocol::Reading& current,
                                     const common::protocol::Reading& last) const;
    void logReading(const common::protocol::Reading& reading) const;
    void raise(Alert alert) const;
    void checkRanges(const common::protocol::Reading& reading) const;
    
    inline void logAlert(const ComparisonResult& result, 
                        const common::protocol::Reading& current,
                        const common::protocol::Reading& last) const;
};
