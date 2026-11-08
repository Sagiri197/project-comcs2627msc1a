#include "server_stats.h"

#include <nlohmann/json.hpp>

#include <ctime>

namespace {

std::string iso_utc(ServerStats::Clock::time_point at) {
    const std::time_t seconds = ServerStats::Clock::to_time_t(at);
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

long long seconds_between(ServerStats::Clock::time_point from, ServerStats::Clock::time_point to) {
    return std::chrono::duration_cast<std::chrono::seconds>(to - from).count();
}

}  // namespace

void ServerStats::reading_received(const std::string& node_id, Clock::time_point at) {
    ++m_readings;
    std::lock_guard lock(m_last_mutex);
    m_last_node = node_id;
    m_last_reading = at;
}

std::string ServerStats::heartbeat_json(std::uint16_t udp_port, Clock::time_point now) const {
    nlohmann::json heartbeat = {
        {"status", "online"},
        {"dateIssued", iso_utc(now)},
        {"uptimeSeconds", seconds_between(m_started, now)},
        {"udpPort", udp_port},
        {"readingsReceived", m_readings.load()},
        {"invalidDatagrams", m_invalid_datagrams.load()},
        {"storageErrors", m_storage_errors.load()},
        {"lastReading", nullptr},
    };

    std::lock_guard lock(m_last_mutex);
    if (!m_last_node.empty()) {
        heartbeat["lastReading"] = {
            {"nodeId", m_last_node},
            {"secondsAgo", seconds_between(m_last_reading, now)},
        };
    }
    return heartbeat.dump();
}
