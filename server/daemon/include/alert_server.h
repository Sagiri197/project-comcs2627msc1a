#pragma once

#include "common/protocol.h"
#include "reading_store.h"
#include "application.h"
#include "udp_socket.h"
#include "server_config.h"
#include "ble_scanner.h"

#include <memory>
#include <mutex>
#include <optional>
#include <cstdint>

class IMqttPublisher;
class AlertPublisher;
class StatusPublisher;

class AlertServer {
public:
    /** @brief A decoded message, its sender, and an optional reading comparison. */
    struct ReceivedMessage {
        common::protocol::Message message;
        UdpEndpoint sender;
        std::optional<AlertLogic::ComparisonResult> comparison;
    };

    /** @brief Outcome of a reading received over BLE. */
    struct BleOutcome {
        bool processed;  ///< False for a duplicate (already stored) or a failed reading.
        std::optional<AlertLogic::ComparisonResult> comparison;
    };

    /**
     * @brief Creates a server with a platform UDP socket bound to an endpoint.
     * @param store Reading store used to persist readings; must outlive this server.
     * @param logic Alert processor used for received readings; must outlive this server.
     * @param endpoint Local IPv4 address and port to bind.
     * @param mqtt Optional MQTT settings for publishing alerts and heartbeats.
     * @param ble BLE fallback settings; when enabled, the server creates and owns a platform
     *            scanner. If no adapter is usable, the error is logged and the server keeps
     *            running on UDP only.
     */
    explicit AlertServer(ReadingStore& store, AlertLogic& logic, const UdpEndpoint& endpoint = {"0.0.0.0", 8080},
                        const std::optional<ServerConfig::MqttSettings>& mqtt = std::nullopt,
                        const ServerConfig::BleSettings& ble = {false, {}});

    /**
     * @brief Creates a server using an existing socket
     * @param store Reading store used to persist readings; must outlive this server.
     * @param logic Alert processor used for received readings; must outlive this server.
     * @param socket Socket used for receiving and sending datagrams.
     * @param mqtt Optional MQTT settings for publishing alerts and heartbeats.
     * @param udp_port Optional UDP port for heartbeat messages; if not provided, not included in heartbeats.
     * @param ble_scanner Optional BLE scanner; the server starts and owns it. Null disables BLE.
     * @throws std::invalid_argument if `socket` is null.
     */
    AlertServer(ReadingStore& store, AlertLogic& logic, std::unique_ptr<IUdpSocket> socket,
                const std::optional<ServerConfig::MqttSettings>& mqtt = std::nullopt,
                std::optional<std::uint16_t> udp_port = std::nullopt,
                std::unique_ptr<IBleScanner> ble_scanner = nullptr);

    /** @brief Stops BLE scanning before the state its callback uses is destroyed. */
    ~AlertServer();

    /**
     * @brief Receives, decodes, and processes one datagram.
     * @return The decoded message, sender endpoint, and any reading comparison.
     * @throws PersistenceError if a reading cannot be stored.
     * @throws std::exception if receiving or decoding the datagram fails.
     */
    ReceivedMessage receive();

    /** @brief Encodes and sends a protocol message to a destination endpoint. */
    void send(const common::protocol::Message& message, const UdpEndpoint& destination) const;

    /**
     * @brief Processes a reading received over BLE with the same alert logic as UDP.
     *
     * BLE is one-way and best effort: nothing is sent back. A node repeats its
     * advertisement many times and may later replay the reading over UDP, so a reading
     * whose identity is already stored is ignored. BLE readings carry no location; it is
     * copied from the node's previous stored reading (or left at 0, 0).
     * Thread-safe with respect to receive(); never throws.
     */
    BleOutcome handleBleReading(common::protocol::Reading reading);

private:
    /**
     * @brief Starts `scanner`, decoding each advertisement and passing it to handleBleReading().
     * On failure the error is logged and BLE stays off; UDP keeps working.
     */
    void startBleScanner(std::unique_ptr<IBleScanner> scanner);

    /** @brief Runs alert logic, stores, and publishes a reading. Caller holds m_processing_mutex. */
    std::optional<AlertLogic::ComparisonResult> processReading(const common::protocol::Reading& reading);

    std::unique_ptr<IUdpSocket> m_socket;
    ReadingStore& m_store;
    AlertLogic& m_logic;
    std::unique_ptr<IMqttPublisher> m_mqtt_publisher;
    std::unique_ptr<AlertPublisher> m_alert_publisher;
    std::unique_ptr<StatusPublisher> m_status_publisher;
    // Readings arrive from the UDP loop and from the BLE scanner thread; the store,
    // alert logic and publishers are not thread-safe.
    std::mutex m_processing_mutex;
    // Declared last so it is destroyed first: its thread calls into the members above.
    std::unique_ptr<IBleScanner> m_ble_scanner;
};