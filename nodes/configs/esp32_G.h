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
    .serial_baud = 115200,
    .time_sync_threshold_ms = 10000,
    .response_timeout_ms = 1000,
    .reading_delay_ms = 3000,
    .status_led_enabled = false,
    .status_red_pin = 26,
    .status_green_pin = 27,
    .status_blue_pin = 14,
    .status_led_common_anode = false,
    .display_enabled = false,
    .display_clk_pin = 18,
    .display_dio_pin = 19,
    .display_brightness = 2
};

}
