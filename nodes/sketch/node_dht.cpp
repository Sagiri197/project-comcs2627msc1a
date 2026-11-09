#include "node_dht.h"

#include <Arduino.h>
#include <DHT.h>

#include "../config.h"

namespace {
DHT dht(node::config.dht_pin, DHT11);
}

void dhtBegin() {
    dht.begin();
}

bool dhtRead(float& temperature, float& humidity) {
    humidity = dht.readHumidity();
    temperature = dht.readTemperature();

    if (isnan(humidity) || isnan(temperature)) {
        Serial.println("Could not read DHT11 sensor");
        return false;
    }
    return true;
}
