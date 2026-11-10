#pragma once

#include "../config_types.h"

namespace node {

inline constexpr DhtConfig dhtConfig{
    .data_pin = 21
};

inline constexpr StatusLedConfig statusLedConfig{
    .red_pin = 4,
    .green_pin = 5,
    .blue_pin = 6,
    .common_anode = false
};

inline constexpr DisplayConfig displayConfig{
    .clock_pin = 18,
    .data_pin = 17,
    .brightness = 2,
    .button_pin = 7
};

inline constexpr Config config{
    .hardware = Hardware::Esp32S3,
    .node_id = "esp32-003",
    .wifi_ssid = "comcs-cell1",
    .wifi_password = "comcs2627",
    .server_address = "as.comcs.lan",
    .server_port = 8080,
    .local_port = 4210,
    .mqtt_host = "cc.comcs.lan",
    .mqtt_port = 1883,
    .dht = &dhtConfig,
    .longitude = -8.620,
    .latitude = 41.160,
    .qos = common::protocol::Qos::BestEffort,
    .serial_baud = 115200,
    .time_sync_threshold_ms = 10000,
    .response_timeout_ms = 1000,
    .reading_delay_ms = 3000,
    .status_led = &statusLedConfig,
    .display = &displayConfig
};

}
