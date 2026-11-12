#include "alert_server.h"

#include <stdexcept>
#include <utility>
#include <spdlog/spdlog.h>

AlertServer::AlertServer(ReadingStore& store, AlertLogic& logic, const UdpEndpoint& endpoint)
    : AlertServer(store, logic, make_udp_socket(endpoint)) {}

AlertServer::AlertServer(ReadingStore& store, AlertLogic& logic, std::unique_ptr<IUdpSocket> socket)
    : m_socket(std::move(socket)), m_store(store), m_logic(logic) {
    if (!m_socket) throw std::invalid_argument("AlertServer needs a socket");
}

AlertServer::ReceivedMessage AlertServer::receive() {

    auto datagram = m_socket->recv();
    std::optional<AlertLogic::ComparisonResult> comparison;
    
    try {
        auto message = common::protocol::decode(datagram.data);

        if (const auto* reading = std::get_if<common::protocol::Reading>(&message)) {
            
            try {

                comparison = m_logic.processReading(*reading);
                
                // Save the reading to database
                m_store.save(*reading);

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

void AlertServer::send(const common::protocol::Message& message,
                       const UdpEndpoint& destination) const {
    m_socket->send(common::protocol::encode(message), destination);
}

