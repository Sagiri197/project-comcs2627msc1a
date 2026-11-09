#pragma once

#include "../config_types.h"

class StatusLed {
public:
    /**
     * @brief Status states displayed by the RGB LED.
     * @note Blue: Wi-Fi disconnected; red: sensor failure; magenta: UDP failure;
     * yellow: time unsynchronized; cyan: MQTT disconnected; green: ready.
     */
    enum class State {
        WifiDisconnected,
        SensorError,
        TransmissionError,
        TimeUnsynchronized,
        MqttDisconnected,
        Ready
    };

    explicit StatusLed(const node::StatusLedConfig& config);

    void begin();
    void update(bool wifiConnected, bool sensorError, bool transmissionError,
                bool timeSynchronized, bool mqttConnected);

private:
    void setColor(bool red, bool green, bool blue);

    bool m_commonAnode;
    std::uint8_t m_redPin;
    std::uint8_t m_greenPin;
    std::uint8_t m_bluePin;
};