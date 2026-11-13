#include "email_publisher.h"
#include <spdlog/spdlog.h>
#include <sstream>
#include <iomanip>
#include <cstdlib>
#include <memory>
#include <array>

EmailNotifier::EmailNotifier(const std::string& webhook_url,
                             const std::string& recipient_email)
    : m_webhook_url(webhook_url), m_recipient_email(recipient_email) {}

EmailNotifier::~EmailNotifier() = default;

std::string EmailNotifier::buildAlertJson(const AlertData& alert_data) const {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);
    
    oss << "{"
        << "\"node_id\":\"" << alert_data.node_id << "\","
        << "\"temperature\":" << alert_data.temperature << ","
        << "\"humidity\":" << alert_data.humidity << ","
        << "\"alert_type\":\"" << alert_data.alert_type << "\","
        << "\"message\":\"" << alert_data.message << "\","
        << "\"email\":\"" << m_recipient_email << "\","
        << "\"subject\":\"Alert - " << alert_data.alert_type << " from Node " << alert_data.node_id << "\""
        << "}";
    
    return oss.str();
}

bool EmailNotifier::sendAlert(const AlertData& alert_data) {
    std::string json_payload = buildAlertJson(alert_data);

    spdlog::info("Sending alert to Power Automate for node {}", alert_data.node_id);
    spdlog::debug("Payload: {}", json_payload);

    std::ostringstream cmd;
    cmd << "curl -X POST '" << m_webhook_url << "' "
        << "-H 'Content-Type: application/json' "
        << "-d '" << json_payload << "' "
        << "-w '%{http_code}' "
        << "-s -o /dev/null";

    spdlog::debug("Executing: {}", cmd.str());

    int result = std::system(cmd.str().c_str());
    
    if (result != 0) {
        spdlog::error("Failed to send alert to Power Automate: system() returned {}", result);
        return false;
    }

    spdlog::info("Alert sent successfully to Power Automate");
    return true;
}
