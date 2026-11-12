#pragma once

#include <string>
#include <cerrno>
#include <sys/socket.h>
#include <netinet/in.h>
#include <optional>

class FakeUdpClient {
public:
    struct Config {
        std::string address;
        int timeout_seconds;

        Config(const std::string& addr = "127.0.0.1", int timeout = 1)
            : address(addr), timeout_seconds(timeout) {}
    };

    explicit FakeUdpClient(const Config& config = Config{});
    ~FakeUdpClient();

    FakeUdpClient(const FakeUdpClient&) = delete;
    FakeUdpClient& operator=(const FakeUdpClient&) = delete;
    FakeUdpClient(FakeUdpClient&&) = delete;
    FakeUdpClient& operator=(FakeUdpClient&&) = delete;

    struct Endpoint {
        std::string address;
        int port;
    };

    Endpoint local_endpoint() const;

    std::string receive();

    std::optional<Endpoint> last_sender() const;

    void send(const std::string& data, const Endpoint& destination);

    void expect_no_reply();

private:
    int m_socket_fd;
    struct sockaddr_in m_server_address;
    Endpoint m_endpoint;
    std::optional<Endpoint> m_last_sender;
};
