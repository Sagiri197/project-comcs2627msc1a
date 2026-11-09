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
    .mqtt_host = "cc.comcs.lan",
    .mqtt_port = 1883,
    .dht_pin = 21,
    .longitude = -8.620,
    .latitude = 41.160,
    .qos = common::protocol::Qos::BestEffort,
    .serial_baud = 115200,
    .time_sync_threshold_ms = 10000,
    .response_timeout_ms = 1000,
    .reading_delay_ms = 3000,
    .status_led_enabled = true,
    .status_red_pin = 4,
    .status_green_pin = 5,
    .status_blue_pin = 6,
    .status_led_common_anode = false,
    .display_enabled = true,
    .display_clk_pin = 18,
    .display_dio_pin = 17,
    .display_brightness = 2
};

}
