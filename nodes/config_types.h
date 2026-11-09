#pragma once

/** @file config_types.h
 * @brief Hardware and runtime configuration types shared by node profiles.
 */

#if __has_include("src/common/protocol.h")
#include "src/common/protocol.h"
#else
#include "../common/protocol.h"
#endif

namespace node {

/** @brief Supported ESP32 board families. */
enum class Hardware {
    Esp32, ///< ESP32 board.
    Esp32S3 ///< ESP32-S3 board.
};

/** @brief Compile-time device settings supplied by the selected node profile. */
struct Config {
    Hardware hardware; ///< Board family for this profile.
    const char* node_id; ///< Device identifier used in protocol identities and MQTT topics.
    const char* wifi_ssid; ///< Wi-Fi network name.
    const char* wifi_password; ///< Wi-Fi network password.
    const char* server_address; ///< UDP alert server hostname or IP address.
    std::uint16_t server_port; ///< Destination UDP port of the alert server.
    std::uint16_t local_port; ///< Local UDP port used to receive responses.
    const char* mqtt_host; ///< Command Centre broker hostname or IP address.
    std::uint16_t mqtt_port; ///< MQTT broker TCP port.
    std::uint8_t dht_pin; ///< GPIO connected to the DHT11 data signal.
    double longitude; ///< Sensor longitude in geographic degrees.
    double latitude; ///< Sensor latitude in geographic degrees.
    common::protocol::Qos qos; ///< Reading's protocol QoS, independent of MQTT's QoS 1.
    unsigned long serial_baud; ///< Serial interface baud rate.
    unsigned long time_sync_threshold_ms; ///< Initial clock synchronization timeout in milliseconds.
    unsigned long response_timeout_ms; ///< UDP response polling window in milliseconds.
    unsigned long reading_delay_ms; ///< Delay after response polling in milliseconds.
};

}