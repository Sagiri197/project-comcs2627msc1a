#pragma once

#include <chrono>
#include <cstdint>
#include <string>

struct ServerConfig {
    struct UdpSettings {
        std::string bind_address;
        std::uint16_t port;
    };

    struct MqttSettings {
        std::string host;
        std::uint16_t port;
        std::string client_id;
        std::string topic;
        std::chrono::milliseconds heartbeat_interval;
        int keepalive_seconds;
    };

    struct EmailSettings {
        bool enabled;
        std::string webhook_url;
        std::string recipient_email;
    };

    /** @brief BLE fallback: readings advertised by nodes that lost WiFi. */
    struct BleSettings {
        bool enabled;
        std::string adapter;   ///< Adapter identifier or address; empty = first adapter.
    };

    std::string database_path;
    UdpSettings udp;
    bool mqtt_enabled;
    MqttSettings mqtt;
    EmailSettings email;
    BleSettings ble;
};

ServerConfig load_server_config(const std::string& path);
