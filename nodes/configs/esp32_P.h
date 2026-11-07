#pragma once

#include "../config_types.h"

namespace node {

inline constexpr Config config{
    .node_id = "esp32-002",
    .wifi_ssid = "Redmi Note 13",
    .wifi_password = "plplplpl",
    .server_address = "10.177.197.123",
    .server_port = 8080,
    .local_port = 4210,
    .longitude = -8.608,
    .latitude = 41.178,
    .sample_timestamp = "2026-11-03T20:30:00Z",
    .sample_temperature = 22.5,
    .sample_relative_humidity = 0.65,
    .qos = common::protocol::Qos::BestEffort,
    .time_synchronized = true,
    .serial_baud = 9600,
    .response_timeout_ms = 2000,
    .reading_delay_ms = 3000
};

}
