#pragma once

#include <string>
#include <string_view>

namespace common::mqtt {

inline constexpr char topic_prefix[] = "comcs/cell1";

inline std::string node_topic(std::string_view node_id) {
    return std::string(topic_prefix) + "/" + std::string(node_id);
}

inline std::string weather_topic(std::string_view node_id) {
    return node_topic(node_id) + "/weather";
}

inline std::string status_topic(std::string_view base_topic) {
    return std::string(base_topic) + "/status";
}

inline std::string heartbeat_topic(std::string_view base_topic) {
    return std::string(base_topic) + "/heartbeat";
}

inline std::string alert_topic(std::string_view base_topic) {
    return std::string(base_topic) + "/alert";
}

}