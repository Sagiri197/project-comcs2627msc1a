#include "udp_socket_posix.h"

#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <stdexcept>
#include <string>
#include <system_error>
#include <type_traits>

class UdpSocketTest : public testing::Test {
protected:
    PosixUdpSocket socket;
    int peer = -1;
    UdpEndpoint endpoint;

    void SetUp() override {
        peer = ::socket(AF_INET, SOCK_DGRAM, 0);
        ASSERT_NE(peer, -1);

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        ASSERT_EQ(bind(peer, reinterpret_cast<const sockaddr*>(&address), sizeof(address)), 0);

        socklen_t size = sizeof(address);
        ASSERT_EQ(getsockname(peer, reinterpret_cast<sockaddr*>(&address), &size), 0);
        endpoint = {"127.0.0.1", ntohs(address.sin_port)};

        timeval timeout{1, 0};
        ASSERT_EQ(setsockopt(peer, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)), 0);
    }

    void TearDown() override {
        if (peer != -1) {
            close(peer);
        }
    }
};

class UdpPayloadTest : public UdpSocketTest,
                       public testing::WithParamInterface<std::string> {};

TEST_P(UdpPayloadTest, SendsAndReceivesDatagrams) {
    const auto& payload = GetParam();
    socket.send(payload, endpoint);

    std::string buffer(65535, '\0');
    sockaddr_in sender{};
    socklen_t size = sizeof(sender);
    const auto received = recvfrom(peer, buffer.data(), buffer.size(), 0,
                                   reinterpret_cast<sockaddr*>(&sender), &size);
    ASSERT_EQ(received, static_cast<ssize_t>(payload.size()));
    buffer.resize(static_cast<std::size_t>(received));
    EXPECT_EQ(buffer, payload);

    ASSERT_EQ(sendto(peer, payload.data(), payload.size(), 0,
                     reinterpret_cast<const sockaddr*>(&sender), size), received);

    const auto datagram = socket.recv();
    EXPECT_EQ(datagram.data, payload);
    EXPECT_EQ(datagram.sender.address, endpoint.address);
    EXPECT_EQ(datagram.sender.port, endpoint.port);

    socket.send("Hello Client", datagram.sender);
    buffer.resize(65535);
    ASSERT_EQ(recvfrom(peer, buffer.data(), buffer.size(), 0, nullptr, nullptr), 12);
    EXPECT_EQ(buffer.substr(0, 12), "Hello Client");
}

INSTANTIATE_TEST_SUITE_P(DatagramSizes, UdpPayloadTest, testing::Values(
        std::string{}, 
        std::string{"alert test"},
        std::string{"alert\0binary", 12}, 
        std::string(65507, 'x')
    ),
    [](const testing::TestParamInfo<std::string>& parameter) {
        return "Bytes" + std::to_string(parameter.param.size());
    });

TEST(UdpSocket, RejectsInvalidBindAddress) {
    EXPECT_THROW((PosixUdpSocket{{"invalid-address", 0}}), std::invalid_argument);
}

TEST_F(UdpSocketTest, RejectsInvalidDestinationAddress) {
    EXPECT_THROW(socket.send("alert", {"invalid-address", 12345}), std::invalid_argument);
}

TEST_F(UdpSocketTest, RejectsOversizedDatagrams) {
    EXPECT_THROW(socket.send(std::string(65508, 'x'), endpoint), std::system_error);
}

TEST_F(UdpSocketTest, RejectsBindingAnOccupiedPort) {
    EXPECT_THROW((PosixUdpSocket{endpoint}), std::system_error);
}