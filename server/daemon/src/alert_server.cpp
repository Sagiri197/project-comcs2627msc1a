#include "alert_server.h"

#include <utility>

AlertServer::AlertServer(ReadingStore& store, const UdpEndpoint& endpoint)
    : m_socket(endpoint), m_store(store) {}

AlertServer::ReceivedMessage AlertServer::receive() const {
    auto datagram = m_socket.recv();
    return {common::protocol::decode(datagram.data), std::move(datagram.sender)};
}

void AlertServer::send(const common::protocol::Message& message,
                       const UdpEndpoint& destination) const {
    m_socket.send(common::protocol::encode(message), destination);
}

ReadingStore::SaveResult AlertServer::accept(const common::protocol::Reading& reading,
                                            const UdpEndpoint& sender) {
    const auto result = m_store.save(reading);
    if (result == ReadingStore::SaveResult::IdentityConflict) {
        send(common::protocol::Reject{reading.identity, common::protocol::RejectCode::IdentityConflict,
                                      "Identity already belongs to a different reading"}, sender);
    } else if (reading.qos == common::protocol::Qos::AtLeastOnce) {
        send(common::protocol::Ack{reading.identity}, sender);
    }
    return result;
}