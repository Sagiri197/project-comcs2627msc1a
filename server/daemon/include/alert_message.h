#pragma once

#include "application.h"

#include <chrono>
#include <string>

// The payload published on comcs/cell1/alerts: a Smart Data Model "Alert" (key-values), e.g.
// {"id":"urn:ngsi-ld:Alert:cell1:esp32-001:boot-1:42:temperatureDifferential","type":"Alert",
//  "category":"environment","subCategory":"temperatureDifferential","severity":"medium",
//  "dateIssued":"2026-11-02T14:30:02Z","description":"Temperature differential 2.6 °C ...",
//  "alertSource":"urn:ngsi-ld:Device:alert-server",
//  "data":{"value":2.6,"threshold":2,"nodes":["esp32-002","esp32-001"],
//          "reading":{"nodeId":"esp32-001","sessionId":"boot-1","seq":42}}}
// The id is derived from the reading identity, so a retried reading yields the same id and the
// Command Centre discards the duplicate.
std::string alert_json(const AlertLogic::Alert& alert,
                       std::chrono::system_clock::time_point now = std::chrono::system_clock::now());
