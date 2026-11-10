#include "alert_server.h"
#include "alert_publisher.h"
#include "application.h"
#include "server_stats.h"
#include "status_publisher.h"
#include "server_config.h"

#include "logging.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

namespace {

void print_usage() {
    std::cout <<
        "Usage: alert_server [--config FILE] [--help]\n"
        "  --config FILE  INI settings file (default server/alert_server.ini)\n"
        "  --help         show this help\n";
}

std::string parse_config_path(int argc, char** argv) {
    
    std::string config_path = "server/alert_server.ini";
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--config") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--config needs a file path");
            }
            config_path = argv[++i];
        } else if (arg == "-h" || arg == "--help") {
            print_usage();
            std::exit(0);
        } else {
            throw std::invalid_argument("unknown option " + arg);
        }
    }
    return config_path;
}

std::atomic<bool> stop_requested{false};

extern "C" void request_stop(int signal) {
    stop_requested = true;
    std::signal(signal, SIG_DFL);
}

}  // namespace

int main(int argc, char** argv) {
    initialize_logger();

    try {
        const ServerConfig config = load_server_config(parse_config_path(argc, argv));

        ReadingStore store(config.database_path);
        AlertLogic logic(store);
        AlertServer server(store, logic, {config.udp.bind_address, config.udp.port});
        spdlog::info("UDP server listening on port {}", config.udp.port);

        std::unique_ptr<MqttPublisher> mqtt;
        std::unique_ptr<StatusPublisher> status;
        std::unique_ptr<AlertPublisher> alerts;
        if (config.mqtt_enabled) {
            mqtt = std::make_unique<MqttPublisher>(config.mqtt);
            status = std::make_unique<StatusPublisher>(*mqtt, config.mqtt, config.udp.port);
            alerts = std::make_unique<AlertPublisher>(*mqtt, config.mqtt);
        } else {
            spdlog::info("MQTT disabled by configuration");
        }

        std::signal(SIGINT, request_stop);
        std::signal(SIGTERM, request_stop);
        std::thread([&mqtt, &status, &alerts] {
            while (!stop_requested) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            spdlog::info("Stopping");
            alerts.reset();
            status.reset();
            mqtt.reset();   // publishes "offline" and disconnects
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
                    if (status) status->reading_received(reading->identity.node_id);
                    if (alerts && received.comparison) alerts->publish(*reading, *received.comparison);
                }

            } catch (const common::protocol::ProtocolError& error) {
                if (status) status->invalid_datagram();
                spdlog::warn("Ignoring invalid protocol datagram: {}", error.what());
            } catch (const PersistenceError& error) {
                if (status) status->storage_error();
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
