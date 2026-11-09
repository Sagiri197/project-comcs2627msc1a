#pragma once

/** @brief Initialize the DHT11 sensor on node::Config::dht_pin. */
void dhtBegin();

/**
 * @brief Read both DHT11 measurements, logging invalid readings to Serial.
 * @pre Call dhtBegin() before acquiring measurements.
 * @param[out] temperature Temperature in degrees Celsius; may be NaN on failure.
 * @param[out] humidity Relative humidity in percent (0-100); may be NaN on failure.
 * @return true if both values are valid, false if either value is NaN.
 */
bool dhtRead(float& temperature, float& humidity);
