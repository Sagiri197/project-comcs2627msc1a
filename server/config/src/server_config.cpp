#include "server_config.h"

#include "ini_reader.h"

#include <chrono>
#include <cstdint>
#include <stdexcept>

namespace {

std::uint16_t read_port(const IniReader& reader, const std::string& section, const std::string& key) {
    const int port = reader.get_int(section, key);
    if (port < 1 || port > 65535) {
        throw std::invalid_argument("[" + section + "] " + key + " must be between 1 and 65535");
    }
    return static_cast<std::uint16_t>(port);
}

}  // namespace

ServerConfig load_server_config(const std::string& path) {
    const IniReader reader(path);
    ServerConfig config{};

    config.database_path = reader.get("database", "path");
    if (config.database_path.empty()) {
        throw std::invalid_argument("[database] path must not be empty");
    }

    config.udp.bind_address = reader.get("udp", "bind_address");
    if (config.udp.bind_address.empty()) {
        throw std::invalid_argument("[udp] bind_address must not be empty");
    }
    config.udp.port = read_port(reader, "udp", "port");

    config.mqtt_enabled = reader.get_bool("mqtt", "enabled");
    config.mqtt.host = reader.get("mqtt", "host");
    config.mqtt.port = read_port(reader, "mqtt", "port");
    config.mqtt.client_id = reader.get("mqtt", "client_id");
    config.mqtt.topic = reader.get("mqtt", "topic");

    const int heartbeat_interval = reader.get_int("mqtt", "heartbeat_interval_seconds");
    if (heartbeat_interval < 1) {
        throw std::invalid_argument("[mqtt] heartbeat_interval_seconds must be at least 1");
    }
    config.mqtt.heartbeat_interval = std::chrono::seconds(heartbeat_interval);

    config.mqtt.keepalive_seconds = reader.get_int("mqtt", "keepalive_seconds");
    if (config.mqtt.keepalive_seconds < 1) {
        throw std::invalid_argument("[mqtt] keepalive_seconds must be at least 1");
    }

    config.email.enabled = reader.get_bool("email", "enabled");
    config.email.webhook_url = reader.get("email", "webhook_url");
    config.email.recipient_email = reader.get("email", "recipient_email");

    if (config.email.enabled) {
        if (config.email.webhook_url.empty()) {
            throw std::invalid_argument("[email] webhook_url must not be empty when email is enabled");
        }
        if (config.email.recipient_email.empty()) {
            throw std::invalid_argument("[email] recipient_email must not be empty when email is enabled");
        }
    }

    return config;
}
