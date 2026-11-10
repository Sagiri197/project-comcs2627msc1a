#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>

class ServerStats {
public:
    using Clock = std::chrono::system_clock;

    /** @brief Creates a statistics collector with the given server start time. */
    explicit ServerStats(Clock::time_point started = Clock::now()) : m_started(started) {}

    /** @brief Records a received reading and updates the most-recent reading details. */
    void reading_received(const std::string& node_id, Clock::time_point at = Clock::now());

    /** @brief Increments the invalid-datagram counter. */
    void invalid_datagram() noexcept { ++m_invalid_datagrams; }

    /** @brief Increments the storage-error counter. */
    void storage_error() noexcept { ++m_storage_errors; }

    /**
     * @brief Serializes the server heartbeat as JSON.
     *
     * The payload includes status, UTC issue time, uptime, UDP port, counters, and the most
     * recent reading. `lastReading` is null until a reading has been recorded.
     * @param udp_port UDP port served by the daemon.
     * @param now Time used for the issue timestamp, uptime, and reading age.
     * @return Compact JSON suitable for publishing on the heartbeat topic.
     */
    std::string heartbeat_json(std::uint16_t udp_port, Clock::time_point now = Clock::now()) const;

private:
    Clock::time_point m_started;
    std::atomic<std::uint64_t> m_readings{0};
    std::atomic<std::uint64_t> m_invalid_datagrams{0};
    std::atomic<std::uint64_t> m_storage_errors{0};

    mutable std::mutex m_last_mutex;
    std::string m_last_node;
    Clock::time_point m_last_reading{};
};
