#pragma once

#include "alert_notifier.h"
#include <string>

class EmailNotifier : public AlertNotifier {
public:
    /**
     * @brief Creates an email notifier that sends alerts via Power Automate webhook.
     * @param webhook_url The Power Automate webhook URL.
     * @param recipient_email The email address to send alerts to.
     */
    EmailNotifier(const std::string& webhook_url, const std::string& recipient_email);

    ~EmailNotifier() override;

    /**
     * @brief Sends an alert via email using the Power Automate webhook.
     * @param alert_data The alert information to publish.
     * @return true if the HTTP request was sent successfully, false otherwise.
     */
    bool sendAlert(const AlertData& alert_data) override;

private:
    std::string m_webhook_url;
    std::string m_recipient_email;

    std::string buildAlertJson(const AlertData& alert_data) const;
};
