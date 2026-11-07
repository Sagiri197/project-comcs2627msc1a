#include "alert_server.h"

#include <utility>

AlertServer::AlertServer(ReadingStore& store, AlertLogic& logic, const UdpEndpoint& endpoint)
    : m_socket(endpoint), m_store(store), m_logic(logic) {}

AlertServer::ReceivedMessage AlertServer::receive() {

    auto datagram = m_socket.recv();
    auto message = common::protocol::decode(datagram.data);

    if (const auto* reading = std::get_if<common::protocol::Reading>(&message)) {
        
        // Accumulate reading
        m_logic.addReading(*reading);
        
        auto accumulated = m_logic.tryProcessSequence(reading->identity.seq);
        
        // Save the reading
        const auto result = m_store.save(*reading);

        if (reading->qos == common::protocol::Qos::AtLeastOnce) {
            send(common::protocol::Ack{reading->identity}, datagram.sender);
        }
    }

    return {std::move(message), std::move(datagram.sender)};
}

void AlertServer::send(const common::protocol::Message& message,
                       const UdpEndpoint& destination) const {
    m_socket.send(common::protocol::encode(message), destination);
}

