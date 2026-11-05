#pragma once

#if __has_include("src/common/protocol.h")
#include "src/common/protocol.h"
#else
#include "../common/protocol.h"
#endif

namespace node {

struct Config {
    const char* node_id;
    const char* wifi_ssid;
    const char* wifi_password;
    const char* server_address;
    std::uint16_t server_port;
    std::uint16_t local_port;
    double longitude;
    double latitude;
    const char* sample_timestamp;
    double sample_temperature;
    double sample_relative_humidity;
    common::protocol::Qos qos;
    bool time_synchronized;
    unsigned long serial_baud;
    unsigned long response_timeout_ms;
    unsigned long reading_delay_ms;
};

}