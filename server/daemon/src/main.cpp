#include "udp_socket.h"

#include "logging.h"

#include <exception>

int main() {
    initialize_logger();

    try {
        UdpSocket server({"0.0.0.0", 8080});
        spdlog::info("UDP server listening on port 8080");

        const auto datagram = server.recv();
        spdlog::info("Client {}:{}: {}", datagram.sender.address,
                     datagram.sender.port, datagram.data);

        server.send("Hello Client", datagram.sender);
        spdlog::info("Hello message sent");

        return 0;
    } catch (const std::exception& error) {
        spdlog::error("Alert server failed: {}", error.what());
        return 1;
    }
}