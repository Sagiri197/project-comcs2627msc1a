#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>

// Counters the receive loop updates and the MQTT heartbeat reports. Safe to update from the
// receive loop while the publisher thread reads them.
class ServerStats {
public:
    using Clock = std::chrono::system_clock;

    explicit ServerStats(Clock::time_point started = Clock::now()) : m_started(started) {}

    void reading_received(const std::string& node_id, Clock::time_point at = Clock::now());
    void invalid_datagram() noexcept { ++m_invalid_datagrams; }
    void storage_error() noexcept { ++m_storage_errors; }

    // The heartbeat payload published on comcs/cell1/alert-server/heartbeat, e.g.
    // {"status":"online","dateIssued":"2026-11-08T21:40:05Z","uptimeSeconds":125,"udpPort":8080,
    //  "readingsReceived":42,"invalidDatagrams":0,"storageErrors":0,
    //  "lastReading":{"nodeId":"esp32-001","secondsAgo":2}}
    // "lastReading" is null until the first reading arrives.
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
