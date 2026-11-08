#pragma once

// UDP link to the alert server.

#include <cstdint>
#include <string>

// Opens the local UDP socket (node::config.local_port).
void udpBegin();

// Sends one datagram to node::config.server_address:server_port.
bool udpSend(const std::string& datagram);

// Polls for a server response (non-blocking) and handles ACK / Reject. The ACK is checked
// against the identity of the reading that was last sent.
void udpReceiveResponse(const std::string& sessionId, std::uint32_t sequenceNumber);
