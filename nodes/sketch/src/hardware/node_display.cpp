#include "node_display.h"

#include <cmath>

TemperatureDisplay::TemperatureDisplay(const node::DisplayConfig& config)
    : m_display(config.clock_pin, config.data_pin), m_brightness(config.brightness) {}

void TemperatureDisplay::begin() {
    m_display.displaySet(m_brightness > 7 ? 7 : m_brightness);
    m_display.displayDP(0);
    showUnavailable();
}

void TemperatureDisplay::showUnavailable() {
    for (std::uint8_t position = 0; position < 3; ++position) {
        m_display.displayNum(position, 20);
    }
}

void TemperatureDisplay::showTemperature(float temperature) {
    const int value = static_cast<int>(std::round(temperature));
    if (value < -99 || value > 999) {
        showUnavailable();
        return;
    }
    if (value < 0) {
        m_display.displayNum(0, 20);
        m_display.displayNum(1, -value >= 10 ? (-value / 10) : 16);
        m_display.displayNum(2, -value % 10);
    } else {
        m_display.displayNum(0, value >= 100 ? value / 100 : 16);
        m_display.displayNum(1, value >= 10 ? (value / 10) % 10 : 16);
        m_display.displayNum(2, value % 10);
    }
}