#pragma once

#include "common/protocol.h"
#include "reading_store.h"
#include "application.h"
#include "udp_socket.h"

#include <memory>

class AlertServer {
public:
    struct ReceivedMessage {
        common::protocol::Message message;
        UdpEndpoint sender;
    };

    // Binds the platform's UDP socket (make_udp_socket) to `endpoint`.
    explicit AlertServer(ReadingStore& store, AlertLogic& logic, const UdpEndpoint& endpoint = {"0.0.0.0", 8080});
    // Uses an already-created socket, e.g. a test double.
    AlertServer(ReadingStore& store, AlertLogic& logic, std::unique_ptr<IUdpSocket> socket);

    ReceivedMessage receive();
    void send(const common::protocol::Message& message, const UdpEndpoint& destination) const;

private:
    
    std::unique_ptr<IUdpSocket> m_socket;
    ReadingStore& m_store;
    
    AlertLogic& m_logic;
};