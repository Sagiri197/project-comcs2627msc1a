#pragma once

#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <cstdint>

class SpyMqttBroker {
public:
    struct Config {
        std::string address;
        uint16_t port;

        Config(const std::string& addr = "127.0.0.1", uint16_t p = 0)
            : address(addr), port(p) {}
    };

    struct PublishedMessage {
        std::string topic;
        std::string payload;
        uint8_t qos;
        bool retained;
    };

    explicit SpyMqttBroker(const Config& config = Config{});
    ~SpyMqttBroker();

    SpyMqttBroker(const SpyMqttBroker&) = delete;
    SpyMqttBroker& operator=(const SpyMqttBroker&) = delete;
    SpyMqttBroker(SpyMqttBroker&&) = delete;
    SpyMqttBroker& operator=(SpyMqttBroker&&) = delete;

    struct Endpoint {
        std::string address;
        uint16_t port;
    };

    Endpoint local_endpoint() const;

    std::vector<PublishedMessage> published_messages() const;

    void clear_messages();

    void wait_for_message(int timeout_ms = 1000);

private:
    int m_socket_fd;
    Endpoint m_endpoint;
    std::vector<PublishedMessage> m_published_messages;
    std::vector<int> m_client_fds;

    void accept_and_handle_client();
    void process_client(int client_fd);
};
