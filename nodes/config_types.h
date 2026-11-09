#pragma once

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

struct DhtConfig {
    std::uint8_t data_pin;
};

struct StatusLedConfig {
    std::uint8_t red_pin;
    std::uint8_t green_pin;
    std::uint8_t blue_pin;
    bool common_anode;
};

struct DisplayConfig {
    std::uint8_t clock_pin;
    std::uint8_t data_pin;
    std::uint8_t brightness;
};

/** @brief Compile-time device settings supplied by the selected node profile. */
struct Config {
    Hardware hardware;
    const char* node_id;
    const char* wifi_ssid;
    const char* wifi_password;
    const char* server_address;
    std::uint16_t server_port;
    std::uint16_t local_port;
    const char* mqtt_host;      // Command Centre broker (cc.comcs.lan on the Pi's AP)
    std::uint16_t mqtt_port;
    const DhtConfig* dht = nullptr;
    double longitude;
    double latitude;
    common::protocol::Qos qos;
    unsigned long serial_baud;
    unsigned long time_sync_threshold_ms;
    unsigned long response_timeout_ms;
    unsigned long reading_delay_ms;
    const StatusLedConfig* status_led = nullptr;
    const DisplayConfig* display = nullptr;
};

}