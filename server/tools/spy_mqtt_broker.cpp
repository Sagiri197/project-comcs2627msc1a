#include "spy_mqtt_broker.h"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include <stdexcept>
#include <thread>
#include <mutex>
#include <cstring>
#include <algorithm>

SpyMqttBroker::SpyMqttBroker(const Config& config)
    : m_socket_fd(-1), m_endpoint{config.address, config.port} {
    m_socket_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (m_socket_fd == -1) {
        throw std::runtime_error("Failed to create TCP socket");
    }

    int reuse = 1;
    if (::setsockopt(m_socket_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) != 0) {
        ::close(m_socket_fd);
        throw std::runtime_error("Failed to set SO_REUSEADDR");
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(config.port);
    if (inet_pton(AF_INET, config.address.c_str(), &address.sin_addr) != 1) {
        ::close(m_socket_fd);
        throw std::invalid_argument("Invalid address: " + config.address);
    }

    if (::bind(m_socket_fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
        ::close(m_socket_fd);
        throw std::runtime_error("Failed to bind TCP socket");
    }

    if (::listen(m_socket_fd, 5) != 0) {
        ::close(m_socket_fd);
        throw std::runtime_error("Failed to listen on TCP socket");
    }

    sockaddr_in bound_address{};
    socklen_t address_size = sizeof(bound_address);
    if (::getsockname(m_socket_fd, reinterpret_cast<sockaddr*>(&bound_address), &address_size) != 0) {
        ::close(m_socket_fd);
        throw std::runtime_error("Failed to get socket name");
    }

    m_endpoint.port = ntohs(bound_address.sin_port);

    // Set non-blocking for accept
    int flags = fcntl(m_socket_fd, F_GETFL, 0);
    if (flags == -1 || fcntl(m_socket_fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        ::close(m_socket_fd);
        throw std::runtime_error("Failed to set non-blocking mode");
    }
}

SpyMqttBroker::~SpyMqttBroker() {
    for (int fd : m_client_fds) {
        if (fd >= 0) {
            ::close(fd);
        }
    }
    if (m_socket_fd != -1) {
        ::close(m_socket_fd);
    }
}

SpyMqttBroker::Endpoint SpyMqttBroker::local_endpoint() const {
    return m_endpoint;
}

std::vector<SpyMqttBroker::PublishedMessage> SpyMqttBroker::published_messages() const {
    return m_published_messages;
}

void SpyMqttBroker::clear_messages() {
    m_published_messages.clear();
}

void SpyMqttBroker::wait_for_message(int timeout_ms) {
    // Clean up disconnected clients
    m_client_fds.erase(
        std::remove_if(m_client_fds.begin(), m_client_fds.end(),
                      [](int fd) { return fd < 0; }),
        m_client_fds.end()
    );

    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(m_socket_fd, &readfds);

    int max_fd = m_socket_fd;
    for (int fd : m_client_fds) {
        FD_SET(fd, &readfds);
        max_fd = std::max(max_fd, fd);
    }

    timeval timeout{};
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;

    int activity = ::select(max_fd + 1, &readfds, nullptr, nullptr, &timeout);
    if (activity <= 0) {
        return;
    }

    // Accept new connections
    if (FD_ISSET(m_socket_fd, &readfds)) {
        accept_and_handle_client();
    }

    // Process existing client connections
    for (int fd : m_client_fds) {
        if (FD_ISSET(fd, &readfds)) {
            process_client(fd);
        }
    }
}

void SpyMqttBroker::accept_and_handle_client() {
    sockaddr_in client_address{};
    socklen_t client_addr_len = sizeof(client_address);

    int client_fd = ::accept(m_socket_fd, reinterpret_cast<sockaddr*>(&client_address), &client_addr_len);
    if (client_fd == -1) {
        return;
    }

    // Set the socket to non-blocking mode for use with select()
    int flags = fcntl(client_fd, F_GETFL, 0);
    if (flags != -1) {
        fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);
    }

    // Try to read CONNECT packet if available
    uint8_t buffer[4096];
    ssize_t received = ::recv(client_fd, buffer, sizeof(buffer), 0);
    
    if (received > 0) {
        // We got the CONNECT packet, send CONNACK
        uint8_t connack[] = {0x20, 0x02, 0x00, 0x00};
        ::send(client_fd, connack, sizeof(connack), 0);
    }
    
    // Add to active clients (even if we haven't received CONNECT yet)
    m_client_fds.push_back(client_fd);
}

void SpyMqttBroker::process_client(int client_fd) {
    uint8_t buffer[4096];
    ssize_t received = ::recv(client_fd, buffer, sizeof(buffer), 0);
    
    if (received == 0) {
        // Client disconnected normally
        ::close(client_fd);
        for (int& fd : m_client_fds) {
            if (fd == client_fd) {
                fd = -1;
                break;
            }
        }
        return;
    }
    
    if (received < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        // Error occurred
        ::close(client_fd);
        for (int& fd : m_client_fds) {
            if (fd == client_fd) {
                fd = -1;
                break;
            }
        }
        return;
    }
    
    if (received <= 0) {
        // No data available right now, but socket is still ok
        return;
    }

    // Parse fixed header
    if (received < 2) return;

    uint8_t byte1 = buffer[0];
    uint8_t msg_type = (byte1 >> 4) & 0x0F;

    // Handle CONNECT packet (type 1) - send CONNACK if not already sent
    if (msg_type == 1) {
        uint8_t connack[] = {0x20, 0x02, 0x00, 0x00};
        ::send(client_fd, connack, sizeof(connack), 0);
        return;
    }

    // Simple PUBLISH message handling (type 3)
    if (msg_type == 3 && received >= 4) {
        size_t pos = 2;

        // Topic length (big-endian)
        uint16_t topic_len = (static_cast<uint16_t>(buffer[pos]) << 8) | buffer[pos + 1];
        pos += 2;

        if (pos + topic_len >= static_cast<size_t>(received)) return;

        std::string topic(reinterpret_cast<const char*>(buffer + pos), topic_len);
        pos += topic_len;

        // QoS > 0 has packet ID
        uint8_t qos = (byte1 >> 1) & 0x03;
        if (qos > 0 && pos + 2 <= static_cast<size_t>(received)) {
            pos += 2;
        }

        // Remaining is payload
        if (pos < static_cast<size_t>(received)) {
            std::string payload(reinterpret_cast<const char*>(buffer + pos), received - pos);
            bool retained = (byte1 & 0x01) != 0;
            m_published_messages.push_back({topic, payload, qos, retained});
        }

        // Send PUBACK if QoS > 0
        if (qos > 0) {
            uint8_t puback[] = {0x40, 0x02, 0x00, 0x00};
            ::send(client_fd, puback, sizeof(puback), 0);
        }
    }
    // Handle PINGREQ (type 12) - respond with PINGRESP
    else if (msg_type == 12) {
        uint8_t pingresp[] = {0xd0, 0x00};
        ::send(client_fd, pingresp, sizeof(pingresp), 0);
    }
    // Handle DISCONNECT (type 14)
    else if (msg_type == 14) {
        ::close(client_fd);
        for (int& fd : m_client_fds) {
            if (fd == client_fd) {
                fd = -1;
                break;
            }
        }
    }
}
