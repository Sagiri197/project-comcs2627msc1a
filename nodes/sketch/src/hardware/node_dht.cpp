#include "node_dht.h"

#include <Arduino.h>

DhtSensor::DhtSensor(const node::DhtConfig& config) : m_sensor(config.data_pin, DHT11) {}

void DhtSensor::begin() {
    m_sensor.begin();
}

bool DhtSensor::read(float& temperature, float& humidity) {
    humidity = m_sensor.readHumidity();
    temperature = m_sensor.readTemperature();

    if (isnan(humidity) || isnan(temperature)) {
        Serial.println("Could not read DHT11 sensor");
        return false;
    }
    return true;
}