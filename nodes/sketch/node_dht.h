#pragma once

// DHT11 temperature/humidity sensor.

void dhtBegin();

// Reads the sensor. Returns false (and logs) if either value could not be read.
// humidity is in percent (0-100), temperature in degrees Celsius.
bool dhtRead(float& temperature, float& humidity);
