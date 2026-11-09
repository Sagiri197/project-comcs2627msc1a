#include "node_status_led.h"

#include <Arduino.h>
#include <map>

namespace {

struct Color {
    bool red;
    bool green;
    bool blue;
};

const std::map<StatusLed::State, Color> statusColors{
    {StatusLed::State::WifiDisconnected, {false, false, true}},
    {StatusLed::State::SensorError, {true, false, false}},
    {StatusLed::State::TransmissionError, {true, false, true}},
    {StatusLed::State::TimeUnsynchronized, {true, true, false}},
    {StatusLed::State::MqttDisconnected, {false, true, true}},
    {StatusLed::State::Ready, {false, true, false}}
};

}  // namespace

StatusLed::StatusLed(const node::StatusLedConfig& config)
    : m_commonAnode(config.common_anode),
      m_redPin(config.red_pin),
      m_greenPin(config.green_pin),
      m_bluePin(config.blue_pin) {}

void StatusLed::begin() {
    pinMode(m_redPin, OUTPUT);
    pinMode(m_greenPin, OUTPUT);
    pinMode(m_bluePin, OUTPUT);
    setColor(false, false, true);
}

void StatusLed::update(bool wifiConnected, bool sensorError, bool transmissionError, bool timeSynchronized, bool mqttConnected) {
    
    State state = State::Ready;
    if (!wifiConnected) {
        state = State::WifiDisconnected;
    } else if (sensorError) {
        state = State::SensorError;
    } else if (transmissionError) {
        state = State::TransmissionError;
    } else if (!timeSynchronized) {
        state = State::TimeUnsynchronized;
    } else if (!mqttConnected) {
        state = State::MqttDisconnected;
    }

    const Color color = statusColors.at(state);
    setColor(color.red, color.green, color.blue);
}

void StatusLed::setColor(bool red, bool green, bool blue) {
    digitalWrite(m_redPin, red != m_commonAnode ? HIGH : LOW);
    digitalWrite(m_greenPin, green != m_commonAnode ? HIGH : LOW);
    digitalWrite(m_bluePin, blue != m_commonAnode ? HIGH : LOW);
}