#pragma once

#include "application.h"
#include "mqtt_publisher.h"

#include <string>

class AlertPublisher {
public:
    AlertPublisher(IMqttPublisher& mqtt, ServerConfig::MqttSettings config);

    bool publish(const common::protocol::Reading& reading,
                 const AlertLogic::ComparisonResult& comparison);

private:
    IMqttPublisher& m_mqtt;
    std::string m_topic;
};