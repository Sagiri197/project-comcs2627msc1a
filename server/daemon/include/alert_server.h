#pragma once

#include "common/protocol.h"
#include "reading_store.h"
#include "application.h"
#include "udp_socket.h"

#include <memory>
#include <optional>

class AlertServer {
public:
    /** @brief A decoded message, its sender, and an optional reading comparison. */
    struct ReceivedMessage {
        common::protocol::Message message;
        UdpEndpoint sender;
        std::optional<AlertLogic::ComparisonResult> comparison;
    };

    /**
     * @brief Creates a server with a platform UDP socket bound to an endpoint.
     * @param store Reading store used to persist readings; must outlive this server.
     * @param logic Alert processor used for received readings; must outlive this server.
     * @param endpoint Local IPv4 address and port to bind.
     */
    explicit AlertServer(ReadingStore& store, AlertLogic& logic, const UdpEndpoint& endpoint = {"0.0.0.0", 8080});

    /**
     * @brief Creates a server using an existing socket
     * @param store Reading store used to persist readings; must outlive this server.
     * @param logic Alert processor used for received readings; must outlive this server.
     * @param socket Socket used for receiving and sending datagrams.
     * @throws std::invalid_argument if `socket` is null.
     */
    AlertServer(ReadingStore& store, AlertLogic& logic, std::unique_ptr<IUdpSocket> socket);

    /**
     * @brief Receives, decodes, and processes one datagram.
     * @return The decoded message, sender endpoint, and any reading comparison.
     * @throws PersistenceError if a reading cannot be stored.
     * @throws std::exception if receiving or decoding the datagram fails.
     */
    ReceivedMessage receive();

    /** @brief Encodes and sends a protocol message to a destination endpoint. */
    void send(const common::protocol::Message& message, const UdpEndpoint& destination) const;

private:
    
    std::unique_ptr<IUdpSocket> m_socket;
    ReadingStore& m_store;
    
    AlertLogic& m_logic;
};