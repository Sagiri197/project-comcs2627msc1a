#include "udp_socket.h"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <stdexcept>
#include <system_error>

constexpr std::size_t PACKET_SIZE = 65535;

namespace {
sockaddr_in socket_address(const UdpEndpoint& endpoint) {
    
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(endpoint.port);

    if (inet_pton(AF_INET, endpoint.address.c_str(), &address.sin_addr) != 1)
        throw std::invalid_argument("Invalid IPv4 address: " + endpoint.address);
    
    return address;
}

UdpEndpoint endpoint_from(const sockaddr_in& address) {

    char buffer[INET_ADDRSTRLEN]{};
    if (inet_ntop(AF_INET, &address.sin_addr, buffer, sizeof(buffer)) == nullptr) 
        throw std::system_error(errno, std::generic_category(), "inet_ntop");
    
        return {buffer, ntohs(address.sin_port)};
}
}

UdpSocket::UdpSocket(const UdpEndpoint& endpoint) : m_descriptor(-1) {
    
    const auto address = socket_address(endpoint);
    m_descriptor = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (m_descriptor == -1)
        throw std::system_error(errno, std::generic_category(), "socket");
    
    if (bind(m_descriptor, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == -1) {
        const int error = errno;
        close(m_descriptor);
        throw std::system_error(error, std::generic_category(), "bind");
    }
}

UdpSocket::~UdpSocket() {
    close(m_descriptor);
}

void UdpSocket::send(std::string_view data, const UdpEndpoint& destination) const {
    
    const auto address = socket_address(destination);
    if (sendto(m_descriptor, data.data(), data.size(), 0,
               reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == -1) {
        throw std::system_error(errno, std::generic_category(), "sendto");
    }
}

UdpDatagram UdpSocket::recv() const {
    
    UdpDatagram datagram{std::string(PACKET_SIZE, 0), {}};
    sockaddr_in sender{};
    socklen_t sender_size = sizeof(sender);
    
    const auto received = recvfrom(m_descriptor, datagram.data.data(), datagram.data.size(), 0,
                                   reinterpret_cast<sockaddr*>(&sender), &sender_size);
    
    if (received == -1) 
        throw std::system_error(errno, std::generic_category(), "recvfrom");
    
    // Resize received data to the actual number of bytes received
    datagram.data.resize(static_cast<std::size_t>(received));
    datagram.sender = endpoint_from(sender);
    
    return datagram;
}