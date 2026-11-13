#pragma once

#include <memory>
#include <string>

class AlertNotifier {
public:
    virtual ~AlertNotifier() = default;

    struct AlertData {
        std::string node_id;
        double temperature;
        double humidity;
        std::string alert_type;
        std::string message;
    };

    /**
     * @brief Sends an alert notification to subscribers.
     * @param alert_data The alert information to publish.
     * @return true if the alert was published successfully, false otherwise.
     */
    virtual bool sendAlert(const AlertData& alert_data) = 0;
};

using AlertNotifierPtr = std::shared_ptr<AlertNotifier>;
