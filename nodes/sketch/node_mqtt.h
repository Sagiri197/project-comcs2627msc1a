#pragma once

/** @file node_mqtt.h
 * @brief MQTT link to the Command Centre using the ESP-IDF esp-mqtt client.
 */

#include <string>

/**
 * @brief Create and start the MQTT client using the configured broker and node ID.
 */
void mqttStart();

/**
 * @brief Return whether the MQTT client is currently connected to its broker.
 */
bool mqttIsConnected();

/**
 * @brief Enqueue a non-retained QoS 1 reading on `comcs/cell1/<node_id>/weather`.
 * @param json Encoded protocol reading, shared with the UDP alert server.
 */
void mqttPublishReading(const std::string& json);
