#include "alert_server.h"
#include "application.h"

#include "logging.h"

#include <exception>

int main() {
    initialize_logger();

    try {
        ReadingStore store("readings.sqlite3");
        AlertLogic logic;
        AlertServer server(store, logic, {"0.0.0.0", 8080});
        spdlog::info("UDP server listening on port 8080");

        while (true) {
            try {
                const auto received = server.receive();
                
                std::visit([&](const auto& message) {
                    spdlog::debug("Received from {}:{}: node={} session={} seq={}",
                                 received.sender.address, received.sender.port,
                                 message.identity.node_id, message.identity.session_id,
                                 message.identity.seq);
                }, received.message);

            } catch (const common::protocol::ProtocolError& error) {
                spdlog::warn("Ignoring invalid protocol datagram: {}", error.what());
            } catch (const PersistenceError& error) {
                spdlog::error("Reading was not acknowledged: {}", error.what());
            }
        }
    } catch (const std::exception& error) {
        spdlog::error("Alert server failed: {}", error.what());
        return 1;
    }
}