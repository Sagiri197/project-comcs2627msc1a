#include "alert_server.h"
#include "application.h"
#include "server_stats.h"
#include "status_publisher.h"

#include "logging.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <thread>

namespace {

struct Options {
    std::uint16_t udp_port = 8080;
    bool mqtt = true;
    StatusPublisher::Config status;
};

void print_usage() {
    std::cout <<
        "Usage: alert_server [--udp-port PORT] [--mqtt-host HOST] [--mqtt-port PORT] [--status-interval SECONDS] [--no-mqtt]\n"
        "  --udp-port PORT           port for the ESP32 readings (default 8080)\n"
        "  --mqtt-host HOST          Command Centre broker (default cc.comcs.lan)\n"
        "  --mqtt-port PORT          broker port (default 1883)\n"
        "  --status-interval SECONDS heartbeat period on comcs/cell1/alert-server/heartbeat (default 5)\n"
        "  --no-mqtt                 do not connect to the broker\n";
}

Options parse_options(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        auto value = [&]() -> std::string {
            if (i + 1 >= argc) {
                throw std::invalid_argument(std::string(arg) + " needs a value");
            }
            return argv[++i];
        };
        if (arg == "--udp-port") {
            options.udp_port = static_cast<std::uint16_t>(std::stoi(value()));
        } else if (arg == "--mqtt-host") {
            options.status.host = value();
        } else if (arg == "--mqtt-port") {
            options.status.port = static_cast<std::uint16_t>(std::stoi(value()));
        } else if (arg == "--status-interval") {
            const int seconds = std::stoi(value());
            if (seconds < 1) {
                throw std::invalid_argument("--status-interval must be at least 1 second");
            }
            options.status.interval = std::chrono::seconds(seconds);
        } else if (arg == "--no-mqtt") {
            options.mqtt = false;
        } else if (arg == "-h" || arg == "--help") {
            print_usage();
            std::exit(0);
        } else {
            throw std::invalid_argument("unknown option " + std::string(arg));
        }
    }
    return options;
}

// Ctrl+C / SIGTERM: the receive loop is blocked in recv(), so a watcher thread does the
// shutdown that matters (MQTT "offline") and ends the process. A second Ctrl+C kills it.
std::atomic<bool> stop_requested{false};

extern "C" void request_stop(int signal) {
    stop_requested = true;
    std::signal(signal, SIG_DFL);
}

}  // namespace

int main(int argc, char** argv) {
    initialize_logger();

    try {
        const Options options = parse_options(argc, argv);

        ReadingStore store("readings.sqlite3");
        AlertLogic logic(store);
        AlertServer server(store, logic, {"0.0.0.0", options.udp_port});
        spdlog::info("UDP server listening on port {}", options.udp_port);

        ServerStats stats;
        std::unique_ptr<StatusPublisher> status;
        if (options.mqtt) {
            status = std::make_unique<StatusPublisher>(options.status,
                [&stats, port = options.udp_port] { return stats.heartbeat_json(port); });
        } else {
            spdlog::info("MQTT disabled (--no-mqtt)");
        }

        std::signal(SIGINT, request_stop);
        std::signal(SIGTERM, request_stop);
        std::thread([&status] {
            while (!stop_requested) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            spdlog::info("Stopping");
            status.reset();   // publishes "offline" and disconnects
            spdlog::shutdown();
            std::_Exit(0);
        }).detach();

        while (true) {
            try {
                const auto received = server.receive();

                std::visit([&](const auto& message) {
                    spdlog::debug("Received from {}:{}: node={} session={} seq={}",
                                 received.sender.address, received.sender.port,
                                 message.identity.node_id, message.identity.session_id,
                                 message.identity.seq);
                }, received.message);

                if (const auto* reading = std::get_if<common::protocol::Reading>(&received.message)) {
                    stats.reading_received(reading->identity.node_id);
                }

            } catch (const common::protocol::ProtocolError& error) {
                stats.invalid_datagram();
                spdlog::warn("Ignoring invalid protocol datagram: {}", error.what());
            } catch (const PersistenceError& error) {
                stats.storage_error();
                spdlog::error("Reading was not acknowledged: {}", error.what());
            }
        }
    } catch (const std::invalid_argument& error) {
        spdlog::error("{}", error.what());
        print_usage();
        return 2;
    } catch (const std::exception& error) {
        spdlog::error("Alert server failed: {}", error.what());
        return 1;
    }
}
