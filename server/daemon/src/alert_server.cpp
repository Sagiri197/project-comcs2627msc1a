#include "alert_server.h"
#include "mqtt_publisher.h"
#include "alert_publisher.h"
#include "status_publisher.h"
#include "common/ble_reading.h"

#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <spdlog/spdlog.h>

namespace {

// Creates the platform scanner when BLE is enabled; null when disabled or unavailable.
std::unique_ptr<IBleScanner> make_configured_ble_scanner(const ServerConfig::BleSettings& ble) {
    if (!ble.enabled) {
        spdlog::info("BLE scanning disabled by configuration");
        return nullptr;
    }
    try {
        return make_ble_scanner(ble.adapter, common::ble::company_id);
    } catch (const std::exception& error) {
        // UDP keeps working without BLE.
        spdlog::error("BLE scanning unavailable: {}", error.what());
        return nullptr;
    }
}

}  // namespace

AlertServer::AlertServer(ReadingStore& store, AlertLogic& logic, const UdpEndpoint& endpoint,
                        const std::optional<ServerConfig::MqttSettings>& mqtt,
                        const ServerConfig::BleSettings& ble)
    : AlertServer(store, logic, make_udp_socket(endpoint), mqtt, endpoint.port,
                  make_configured_ble_scanner(ble)) {}

AlertServer::AlertServer(ReadingStore& store, AlertLogic& logic, std::unique_ptr<IUdpSocket> socket,
                        const std::optional<ServerConfig::MqttSettings>& mqtt,
                        std::optional<std::uint16_t> udp_port,
                        std::unique_ptr<IBleScanner> ble_scanner)
    : m_socket(std::move(socket)), m_store(store), m_logic(logic) {
    if (!m_socket) throw std::invalid_argument("AlertServer needs a socket");

    if (mqtt) {
        m_mqtt_publisher = std::make_unique<MqttPublisher>(*mqtt);
        m_alert_publisher = std::make_unique<AlertPublisher>(*m_mqtt_publisher, *mqtt);
        
        std::uint16_t port = udp_port.value_or(0);
        m_status_publisher = std::make_unique<StatusPublisher>(*m_mqtt_publisher, *mqtt, port);
    }

    // Last, so the BLE thread never sees a partially constructed server.
    if (ble_scanner) startBleScanner(std::move(ble_scanner));
}

AlertServer::~AlertServer() {
    if (m_ble_scanner) m_ble_scanner->stop();
}

void AlertServer::startBleScanner(std::unique_ptr<IBleScanner> scanner) {
    try {
        // Runs on the BLE thread. A bad advertisement repeats too, so each distinct
        // undecodable payload is reported once (the set is bounded).
        scanner->start([this, undecodable = std::set<std::string>{}](
                           std::string_view manufacturer_data) mutable {
            if (auto reading = common::ble::decodeReading(manufacturer_data)) {
                handleBleReading(std::move(*reading));
                return;
            }
            if (undecodable.size() >= 256) undecodable.clear();
            if (undecodable.emplace(manufacturer_data).second) {
                spdlog::warn("Ignoring BLE advertisement that is not a valid reading ({} bytes)",
                             manufacturer_data.size());
            }
        });
        m_ble_scanner = std::move(scanner);
        spdlog::info("BLE scanning enabled");
    } catch (const std::exception& error) {
        // UDP keeps working without BLE.
        spdlog::error("BLE scanning unavailable: {}", error.what());
    }
}

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


