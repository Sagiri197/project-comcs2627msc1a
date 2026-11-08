#pragma once

#include "../config_types.h"

namespace node {

inline constexpr Config config{
    .hardware = Hardware::Esp32,
    .node_id = "esp32-001",
    .wifi_ssid = "Redmi Note 13",
    .wifi_password = "plplplpl",
    .server_address = "as.comcs.lan",   // alert server on the cell AP (access-point/dns.txt)
    .server_port = 8080,
    .local_port = 4210,
    .mqtt_host = "cc.comcs.lan",
    .mqtt_port = 1883,
    .dht_pin = 21,
    .longitude = -8.611,
    .latitude = 41.145,
    .qos = common::protocol::Qos::BestEffort,
    .serial_baud = 9600,
    .time_sync_threshold_ms = 10000,
    .response_timeout_ms = 1000,
    .reading_delay_ms = 3000
};

}
