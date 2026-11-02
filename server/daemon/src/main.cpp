#include "alert_server.h"

#include "logging.h"

#include <exception>

int main() {
    initialize_logger();

    try {
        ReadingStore store("readings.sqlite3");
        AlertServer server(store, {"0.0.0.0", 8080});
        spdlog::info("UDP server listening on port 8080");

        while (true) {
            try {
                const auto received = server.receive();
                if (const auto* reading = std::get_if<common::protocol::Reading>(&received.message)) {
                    const auto result = server.accept(*reading, received.sender);
                    if (result == ReadingStore::SaveResult::IdentityConflict) {
                        spdlog::warn("Rejected conflicting reading from node={} session={} seq={}",
                                     reading->identity.node_id, reading->identity.session_id,
                                     reading->identity.seq);
                        continue;
                    }
                }
                std::visit([&](const auto& message) {
                    spdlog::info("Client {}:{}: node={} session={} seq={}",
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