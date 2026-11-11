#include "alert_message.h"
#include "server_stats.h"

#include <nlohmann/json.hpp>

#include <string>

std::string alert_json(const AlertLogic::Alert& alert, std::chrono::system_clock::time_point now) {
    const auto& reading = alert.identity;
    const nlohmann::json message = {
        {"id", "urn:ngsi-ld:Alert:cell1:" + reading.node_id + ":" + reading.session_id + ":" +
                   std::to_string(reading.seq) + ":" + alert.sub_category},
        {"type", "Alert"},
        {"category", "environment"},
        {"subCategory", alert.sub_category},
        {"severity", alert.severity},
        {"dateIssued", iso_utc(now)},
        {"description", alert.description},
        {"alertSource", "urn:ngsi-ld:Device:alert-server"},
        {"data",
         {
             {"value", alert.value},
             {"threshold", alert.threshold},
             {"nodes", alert.nodes},
             {"reading",
              {{"nodeId", reading.node_id}, {"sessionId", reading.session_id}, {"seq", reading.seq}}},
         }},
    };
    return message.dump();
}
