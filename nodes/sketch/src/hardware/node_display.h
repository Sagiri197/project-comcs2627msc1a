#pragma once

#include "../config_types.h"
#include "../vendor/GotekLEDC68/TM1651.h"

class TemperatureDisplay {
public:
    explicit TemperatureDisplay(const node::DisplayConfig& config);

    void begin();
    void update();
    void showUnavailable();
    void showReadings(float temperature, float humidity);

private:
    void render();
    void showValue(float reading);

    TM1651 m_display;
    std::uint8_t m_brightness;
    int m_buttonPin;
    bool m_lastButtonState = true;
    bool m_buttonState = true;
    unsigned long m_buttonChangedAt = 0;
    bool m_showHumidity = false;
    bool m_hasReading = false;
    float m_temperature = 0;
    float m_humidity = 0;
};