#include "alert_publisher.h"
#include "common/mqtt_topics.h"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <chrono>
#include <ctime>
#include <string>

namespace {

std::string iso_utc_now() {
    const std::time_t seconds = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc, &seconds);
#else
    gmtime_r(&seconds, &utc);
#endif
    char text[sizeof "2026-11-08T21:40:05Z"]{};
    std::strftime(text, sizeof text, "%Y-%m-%dT%H:%M:%SZ", &utc);
    return text;
}

}  // namespace

AlertPublisher::AlertPublisher(IMqttPublisher& mqtt, ServerConfig::MqttSettings config)
    : m_mqtt(mqtt), m_topic(common::mqtt::alert_topic(config.topic)) {}

// Publishes a FIWARE Smart Data Model "Alert" (key-values). The id is derived from the reading
// identity, so a resent reading yields the same id and the Command Centre discards the duplicate.
bool AlertPublisher::publish(const common::protocol::Reading& reading,
                             const AlertLogic::ComparisonResult& comparison) {
    const bool has_alert = comparison.exceeds_temp_threshold ||
                           comparison.exceeds_humidity_threshold ||
                           comparison.out_of_temp_range ||
                           comparison.out_of_humidity_range;
    if (!has_alert) return false;

    const auto& identity = reading.identity;

    // The model holds one subCategory: the first triggered condition. The description and
    // `data` list all of them.
    std::string sub_category;
    std::string description = identity.node_id + ":";
    const auto add = [&](const char* category, const std::string& text) {
        if (sub_category.empty()) sub_category = category;
        description += " " + text + ";";
    };
    if (comparison.exceeds_temp_threshold)
        add("temperatureDifferential",
            fmt::format("temperature differential {:.1f} °C exceeds threshold", comparison.temperature_diff));
    if (comparison.exceeds_humidity_threshold)
        add("humidityDifferential",
            fmt::format("humidity differential {:.1f} % exceeds threshold", comparison.humidity_diff));
    if (comparison.out_of_temp_range)
        add("temperatureOutOfRange", "temperature out of equipment range");
    if (comparison.out_of_humidity_range)
        add("humidityOutOfRange", "humidity out of equipment range");
    description.pop_back();   // trailing ';'

    const bool out_of_range = comparison.out_of_temp_range || comparison.out_of_humidity_range;

    const nlohmann::json payload = {
        {"id", fmt::format("urn:ngsi-ld:Alert:{}:{}:{}", identity.node_id, identity.session_id, identity.seq)},
        {"type", "Alert"},
        {"category", "environment"},
        {"subCategory", sub_category},
        {"severity", out_of_range ? "high" : "medium"},
        {"description", description},
        {"dateIssued", iso_utc_now()},
        {"validFrom", reading.payload.date_observed},
        {"alertSource", "urn:ngsi-ld:Device:alert-server"},
        {"data",
         {
             {"nodeId", identity.node_id},
             {"sessionId", identity.session_id},
             {"seq", identity.seq},
             {"temperature", reading.payload.temperature},
             {"relativeHumidity", reading.payload.relative_humidity},
             {"temperatureDiff", comparison.temperature_diff},
             {"humidityDiff", comparison.humidity_diff},
             {"timeDiffMs", comparison.time_diff.count()},
             {"exceedsTemperatureThreshold", comparison.exceeds_temp_threshold},
             {"exceedsHumidityThreshold", comparison.exceeds_humidity_threshold},
             {"temperatureOutOfRange", comparison.out_of_temp_range},
             {"humidityOutOfRange", comparison.out_of_humidity_range},
         }},
    };

    if (!m_mqtt.publish(m_topic, payload.dump(), 1, false)) {
        spdlog::warn("MQTT alert for node {} could not be queued", identity.node_id);
        return false;
    }
    return true;
}
