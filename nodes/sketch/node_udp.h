#pragma once

// UDP link to the alert server.

#include <cstdint>
#include <string>

// Opens the local UDP socket (node::config.local_port).
void udpBegin();

// Sends one datagram to node::config.server_address:server_port.
bool udpSend(const std::string& datagram);

enum class UdpResponse { None, Ack, Reject, Other };

// Polls for a server response (non-blocking) and handles ACK / Reject. Returns Ack / Reject
// only when the response matches the given reading identity; a response for another
// reading (e.g. a late ACK of an earlier retry) returns Other.
UdpResponse udpReceiveResponse(const std::string& sessionId, std::uint32_t sequenceNumber);
