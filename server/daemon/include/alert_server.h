#pragma once

#include "common/protocol.h"
#include "reading_store.h"
#include "application.h"
#include "udp_socket.h"

class AlertServer {
public:
    struct ReceivedMessage {
        common::protocol::Message message;
        UdpEndpoint sender;
    };

    explicit AlertServer(ReadingStore& store, AlertLogic& logic, const UdpEndpoint& endpoint = {"0.0.0.0", 8080});

    ReceivedMessage receive();
    void send(const common::protocol::Message& message, const UdpEndpoint& destination) const;

private:
    
    UdpSocket m_socket;
    ReadingStore& m_store;
    
    AlertLogic& m_logic;
};