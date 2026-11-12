#include "fake_udp_client.h"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <stdexcept>
#include <cstring>

FakeUdpClient::FakeUdpClient(const Config& config)
    : m_socket_fd(-1), m_server_address{}, m_endpoint{config.address, 0} {
    m_socket_fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (m_socket_fd == -1) {
        throw std::runtime_error("Failed to create UDP socket");
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    if (inet_pton(AF_INET, config.address.c_str(), &address.sin_addr) != 1) {
        ::close(m_socket_fd);
        throw std::invalid_argument("Invalid address: " + config.address);
    }
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (::bind(m_socket_fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
        ::close(m_socket_fd);
        throw std::runtime_error("Failed to bind UDP socket");
    }

    socklen_t address_size = sizeof(address);
    if (::getsockname(m_socket_fd, reinterpret_cast<sockaddr*>(&address), &address_size) != 0) {
        ::close(m_socket_fd);
        throw std::runtime_error("Failed to get socket name");
    }

    m_endpoint.port = ntohs(address.sin_port);

    timeval timeout{config.timeout_seconds, 0};
    if (::setsockopt(m_socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) != 0) {
        ::close(m_socket_fd);
        throw std::runtime_error("Failed to set socket timeout");
    }
}

FakeUdpClient::~FakeUdpClient() {
    if (m_socket_fd != -1) {
        ::close(m_socket_fd);
    }
}

FakeUdpClient::Endpoint FakeUdpClient::local_endpoint() const {
    return m_endpoint;
}

std::string FakeUdpClient::receive() {
    std::string wire(65535, '\0');
    socklen_t address_size = sizeof(m_server_address);
    const auto received = ::recvfrom(m_socket_fd, wire.data(), wire.size(), 0,
                                     reinterpret_cast<sockaddr*>(&m_server_address), &address_size);
    if (received < 0) {
        return {};
    }
    char sender_addr[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &m_server_address.sin_addr, sender_addr, INET_ADDRSTRLEN);
    m_last_sender = Endpoint{sender_addr, ntohs(m_server_address.sin_port)};
    wire.resize(static_cast<std::size_t>(received));
    return wire;
}

std::optional<FakeUdpClient::Endpoint> FakeUdpClient::last_sender() const {
    return m_last_sender;
}

void FakeUdpClient::send(const std::string& data, const Endpoint& destination) {
    sockaddr_in dest_addr{};
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(destination.port);
    if (inet_pton(AF_INET, destination.address.c_str(), &dest_addr.sin_addr) != 1) {
        throw std::invalid_argument("Invalid destination address: " + destination.address);
    }

    const auto sent = ::sendto(m_socket_fd, data.data(), data.size(), 0,
                               reinterpret_cast<const sockaddr*>(&dest_addr), sizeof(dest_addr));
    if (sent != static_cast<ssize_t>(data.size())) {
        throw std::runtime_error("Failed to send UDP message");
    }
}

void FakeUdpClient::expect_no_reply() {
    char buffer{};
    errno = 0;
    const auto result = ::recv(m_socket_fd, &buffer, sizeof(buffer), MSG_DONTWAIT);
    if (result != -1 || (errno != EAGAIN && errno != EWOULDBLOCK)) {
        throw std::runtime_error("Expected no reply but received data");
    }
}
