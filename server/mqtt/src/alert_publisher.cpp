#include "alert_publisher.h"
#include "common/mqtt_topics.h"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

AlertPublisher::AlertPublisher(IMqttPublisher& mqtt, ServerConfig::MqttSettings config)
    : m_mqtt(mqtt), m_topic(common::mqtt::alert_topic(config.topic)) {}

bool AlertPublisher::publish(const common::protocol::Reading& reading,
                             const AlertLogic::ComparisonResult& comparison) {
    const bool has_alert = comparison.exceeds_temp_threshold ||
                           comparison.exceeds_humidity_threshold ||
                           comparison.out_of_temp_range ||
                           comparison.out_of_humidity_range;
    if (!has_alert) return false;

    const nlohmann::json payload = {
        {"kind", "alert"},
        {"nodeId", reading.identity.node_id},
        {"sessionId", reading.identity.session_id},
        {"seq", reading.identity.seq},
        {"dateObserved", reading.payload.date_observed},
        {"temperature", reading.payload.temperature},
        {"relativeHumidity", reading.payload.relative_humidity},
        {"temperatureDiff", comparison.temperature_diff},
        {"humidityDiff", comparison.humidity_diff},
        {"timeDiffMs", comparison.time_diff.count()},
        {"exceedsTemperatureThreshold", comparison.exceeds_temp_threshold},
        {"exceedsHumidityThreshold", comparison.exceeds_humidity_threshold},
        {"temperatureOutOfRange", comparison.out_of_temp_range},
        {"humidityOutOfRange", comparison.out_of_humidity_range},
    };

    if (!m_mqtt.publish(m_topic, payload.dump(), 1, false)) {
        spdlog::warn("MQTT alert for node {} could not be queued", reading.identity.node_id);
        return false;
    }
    return true;
}