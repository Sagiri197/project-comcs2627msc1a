#pragma once

#include "../config_types.h"

#include <DHT.h>

class DhtSensor {
public:
    explicit DhtSensor(const node::DhtConfig& config);

    void begin();
    bool read(float& temperature, float& humidity);

private:
    DHT m_sensor;
};