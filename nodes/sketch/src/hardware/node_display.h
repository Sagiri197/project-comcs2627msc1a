#pragma once

#include "../../../config_types.h"
#include "../../../vendor/GotekLEDC68/TM1651.h"

class TemperatureDisplay {
public:
    explicit TemperatureDisplay(const node::DisplayConfig& config);

    void begin();
    void showUnavailable();
    void showTemperature(float temperature);

private:
    TM1651 m_display;
    std::uint8_t m_brightness;
};