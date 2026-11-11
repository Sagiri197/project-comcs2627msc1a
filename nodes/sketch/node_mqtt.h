#pragma once

// MQTT link to the Command Centre.
// Topics and payloads follow command-centre/README.md ("MQTT contract").
// (Named node_mqtt.h to avoid clashing with ESP-IDF's <mqtt_client.h>.)

#include <string>

// Starts the esp-mqtt client; it connects and reconnects in its own task.
void mqttStart();

// True while the client holds a session with the broker.
bool mqttIsConnected();

// QoS 1 publish of a reading on comcs/cell1/<node_id>/weather. Enqueued in the client's
// outbox, so it is sent (and retried until the broker's PUBACK) even if the broker is
// briefly unreachable. Returns false if the reading could not be enqueued.
// The outbox lives in RAM: readings must go to the SPIFFS spool while disconnected.
bool mqttPublishReading(const std::string& json);
