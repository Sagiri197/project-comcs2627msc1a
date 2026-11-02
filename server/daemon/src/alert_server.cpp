#include "alert_server.h"

#include <utility>

AlertServer::AlertServer(ReadingStore& store, const UdpEndpoint& endpoint)
    : m_socket(endpoint), m_store(store) {}

AlertServer::ReceivedMessage AlertServer::receive() {

    auto datagram = m_socket.recv();
    auto message = common::protocol::decode(datagram.data);

    if (const auto* reading = std::get_if<common::protocol::Reading>(&message)) {
        
        const auto result = m_store.save(*reading);
        if (result == ReadingStore::SaveResult::IdentityConflict) {
            send(common::protocol::Reject{reading->identity, common::protocol::RejectCode::IdentityConflict,
                                          "Identity already belongs to a different reading"}, datagram.sender);
            
            throw common::protocol::ProtocolError(common::protocol::ErrorCode::InvalidMessage,
                                                   "Reading identity conflicts with an accepted reading");
        }

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
