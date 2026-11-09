#pragma once

/** @file node_udp.h
 * @brief Datagram transport and response handling for the UDP alert server.
 */

#include <cstdint>
#include <string>

/**
 * @brief Bind the UDP socket to node::Config::local_port.
 */
void udpBegin();

/**
 * @brief Send a datagram to the configured alert server address and port.
 * @pre Call udpBegin() after connecting to Wi-Fi.
 * @param datagram Encoded protocol message to transmit.
 * @return true if the socket finishes sending the packet, false if packet
 * initialization or transmission fails. Success does not imply server acknowledgement.
 */
bool udpSend(const std::string& datagram);

/**
 * @brief Poll once for a response and log ACK identity matches or reject reasons.
 * @param sessionId Boot session ID of the most recently sent reading.
 * @param sequenceNumber Sequence number of the most recently sent reading.
 */
void udpReceiveResponse(const std::string& sessionId, std::uint32_t sequenceNumber);
