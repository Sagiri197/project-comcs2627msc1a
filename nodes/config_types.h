#pragma once

#if __has_include("src/common/protocol.h")
#include "src/common/protocol.h"
#else
#include "../common/protocol.h"
#endif

namespace node {

enum class Hardware { Esp32, Esp32S3 };

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
    std::uint8_t dht_pin;
    double longitude;
    double latitude;
    common::protocol::Qos qos;
    unsigned long serial_baud;
    unsigned long time_sync_threshold_ms;
    unsigned long response_timeout_ms;
    unsigned long reading_delay_ms;
};

}