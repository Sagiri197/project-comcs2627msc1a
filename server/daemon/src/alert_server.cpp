#include "alert_server.h"
#include "mqtt_publisher.h"
#include "alert_publisher.h"
#include "status_publisher.h"

#include <stdexcept>
#include <utility>
#include <spdlog/spdlog.h>

AlertServer::AlertServer(ReadingStore& store, AlertLogic& logic, const UdpEndpoint& endpoint,
                        const std::optional<ServerConfig::MqttSettings>& mqtt)
    : AlertServer(store, logic, make_udp_socket(endpoint), mqtt, endpoint.port) {}

AlertServer::AlertServer(ReadingStore& store, AlertLogic& logic, std::unique_ptr<IUdpSocket> socket,
                        const std::optional<ServerConfig::MqttSettings>& mqtt,
                        std::optional<std::uint16_t> udp_port)
    : m_socket(std::move(socket)), m_store(store), m_logic(logic) {
    if (!m_socket) throw std::invalid_argument("AlertServer needs a socket");

    if (mqtt) {
        m_mqtt_publisher = std::make_unique<MqttPublisher>(*mqtt);
        m_alert_publisher = std::make_unique<AlertPublisher>(*m_mqtt_publisher, *mqtt);
        
        std::uint16_t port = udp_port.value_or(0);
        m_status_publisher = std::make_unique<StatusPublisher>(*m_mqtt_publisher, *mqtt, port);
    }
}

AlertServer::~AlertServer() = default;

AlertServer::ReceivedMessage AlertServer::receive() {
    auto datagram = m_socket->recv();
    std::optional<AlertLogic::ComparisonResult> comparison;
    
    try {
        auto message = common::protocol::decode(datagram.data);

        if (const auto* reading = std::get_if<common::protocol::Reading>(&message)) {
            
            try {
                std::lock_guard<std::mutex> lock(m_processing_mutex);
                comparison = processReading(*reading);
            } catch (const PersistenceError&) {
                throw;
            } catch (const std::exception& e) {
                spdlog::warn("Error processing reading: {}", e.what());
                try {
                    send(common::protocol::Reject{
                        reading->identity,
                        common::protocol::RejectCode::InvalidPayload,
                        std::string("Processing error: ") + e.what()
                    }, datagram.sender);
                } catch (const std::exception& send_error) {
                    spdlog::error("Failed to send reject: {}", send_error.what());
                }
                return {std::move(message), std::move(datagram.sender)};
            }

            if (reading->qos == common::protocol::Qos::AtLeastOnce) {
                send(common::protocol::Ack{reading->identity}, datagram.sender);
            }
        }

        return {std::move(message), std::move(datagram.sender), std::move(comparison)};
    } catch (const common::protocol::ProtocolError& e) {
        spdlog::warn("Decode error: {}", e.what());
        throw;
    }
}

std::optional<AlertLogic::ComparisonResult> AlertServer::processReading(
    const common::protocol::Reading& reading) {
    auto comparison = m_logic.processReading(reading);

    m_store.save(reading);
    if (m_status_publisher) m_status_publisher->reading_received(reading.identity.node_id);

    if (comparison && m_alert_publisher) {
        m_alert_publisher->publish(reading, *comparison);
    }
    return comparison;
}

AlertServer::BleOutcome AlertServer::handleBleReading(common::protocol::Reading reading) {
    try {
        std::lock_guard<std::mutex> lock(m_processing_mutex);

        if (m_store.find(reading.identity)) {
            // Repeated advertisement or already seen over UDP: only visible at debug level.
            spdlog::debug("BLE reading from node {} session={} seq={} already stored, ignored",
                          reading.identity.node_id, reading.identity.session_id, reading.identity.seq);
            return {false, std::nullopt};
        }

        if (auto previous = m_store.getLastReadingBefore(reading.identity)) {
            reading.payload.location = previous->payload.location;
        }

        spdlog::info("Reading received over BLE from node {}: session={} seq={} {:.2f} °C {:.1f} % at {}",
                     reading.identity.node_id, reading.identity.session_id, reading.identity.seq,
                     reading.payload.temperature, reading.payload.relative_humidity * 100.0,
                     reading.payload.date_observed);
        if (m_status_publisher) m_status_publisher->ble_reading_received(reading.identity.node_id);
        return {true, processReading(reading)};
    } catch (const std::exception& e) {
        spdlog::error("BLE reading from node {} not processed: {}", reading.identity.node_id, e.what());
        return {false, std::nullopt};
    }
}

void AlertServer::send(const common::protocol::Message& message,
                       const UdpEndpoint& destination) const {
    m_socket->send(common::protocol::encode(message), destination);
}


