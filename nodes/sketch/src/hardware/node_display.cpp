#include "node_display.h"

#include <cmath>

TemperatureDisplay::TemperatureDisplay(const node::DisplayConfig& config)
    : m_display(config.clock_pin, config.data_pin), m_brightness(config.brightness),
      m_buttonPin(config.button_pin) {}

void TemperatureDisplay::begin() {
    if (m_buttonPin >= 0) {
        pinMode(m_buttonPin, INPUT_PULLUP);
        m_buttonState = m_lastButtonState = digitalRead(m_buttonPin);
        m_buttonChangedAt = millis();
        Serial.printf("Display button: GPIO %d, %s\n", m_buttonPin,
                      m_buttonState ? "released" : "pressed");
    } else {
        Serial.println("Display button: disabled");
    }
    m_display.displaySet(m_brightness > 7 ? 7 : m_brightness);
    m_display.displayDP(0);
    showUnavailable();
}

void TemperatureDisplay::update() {
    if (m_buttonPin < 0) {
        return;
    }
    const bool state = digitalRead(m_buttonPin);
    const unsigned long now = millis();
    if (state != m_lastButtonState) {
        m_lastButtonState = state;
        m_buttonChangedAt = now;
    }
    if (state != m_buttonState && now - m_buttonChangedAt >= 30) {
        m_buttonState = state;
        Serial.printf("Display button: %s\n", state ? "released" : "pressed");
        if (!state) {
            m_showHumidity = !m_showHumidity;
            Serial.printf("Display mode: %s\n", m_showHumidity ? "humidity (%)" : "temperature (C)");
            render();
        }
    }
}

void TemperatureDisplay::showUnavailable() {
    m_hasReading = false;
    for (std::uint8_t position = 0; position < 3; ++position) {
        m_display.displayNum(position, 20);
    }
}

void TemperatureDisplay::showReadings(float temperature, float humidity) {
    m_temperature = temperature;
    m_humidity = humidity;
    m_hasReading = true;
    render();
}

void TemperatureDisplay::render() {
    if (m_hasReading) {
        showValue(m_showHumidity ? m_humidity : m_temperature);
    }
}

void TemperatureDisplay::showValue(float reading) {
    const float rounded = std::round(reading);
    if (!std::isfinite(rounded) || rounded < -99 || rounded > 999) {
        showUnavailable();
        return;
    }
    const int value = static_cast<int>(rounded);
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