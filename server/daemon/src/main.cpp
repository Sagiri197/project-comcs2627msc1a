#include "alert_server.h"
#include "application.h"
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
        "  --config FILE  INI settings file (default server/config/alert_server.ini)\n"
        "  --help         show this help\n";
}

std::string parse_config_path(int argc, char** argv) {
    
    std::string config_path = "server/config/alert_server.ini";
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
        
        // Create AlertServer with optional MQTT support
        std::optional<ServerConfig::MqttSettings> mqtt_config;
        if (config.mqtt_enabled) {
            mqtt_config = config.mqtt;
            spdlog::info("MQTT enabled: connecting to {}:{}", config.mqtt.host, config.mqtt.port);
        } else {
            spdlog::info("MQTT disabled by configuration");
        }
        
        AlertServer server(store, logic, {config.udp.bind_address, config.udp.port}, mqtt_config);
        spdlog::info("UDP server listening on port {}", config.udp.port);

        std::signal(SIGINT, request_stop);
        std::signal(SIGTERM, request_stop);
        std::thread([&mqtt_config] {
            while (!stop_requested) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            spdlog::info("Stopping");
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

            } catch (const common::protocol::ProtocolError& error) {
                spdlog::warn("Ignoring invalid protocol datagram: {}", error.what());
            } catch (const PersistenceError& error) {
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
