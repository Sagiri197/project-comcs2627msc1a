#pragma once

#include "../config_types.h"

namespace node {

inline constexpr Config config{
    .hardware = Hardware::Esp32S3,
    .node_id = "esp32-003",
    .wifi_ssid = "Redmi Note 13",
    .wifi_password = "plplplpl",
    .server_address = "172.19.96.45",
    .server_port = 8080,
    .local_port = 4210,
    .dht_pin = 21,
    .longitude = -8.620,
    .latitude = 41.160,
    .qos = common::protocol::Qos::BestEffort,
    .serial_baud = 9600,
    .time_sync_threshold_ms = 10000,
    .response_timeout_ms = 1000,
    .reading_delay_ms = 3000
};

}
