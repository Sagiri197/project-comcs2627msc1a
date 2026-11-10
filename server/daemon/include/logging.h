#pragma once

#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <memory>

inline constexpr char logger_name[] = "alert_server";

/**
 * @brief Configures the default logger for console and file output.
 *
 * The file sink writes to `LOG_PATH`, relative to the server's working directory, and uses
 * Unix timestamps for log-monitoring scripts.
 */
inline void initialize_logger() {
    auto console = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    console->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] %v");

    auto file = std::make_shared<spdlog::sinks::basic_file_sink_mt>(LOG_PATH);
    file->set_pattern("%E [%l] %v");

    auto logger = std::make_shared<spdlog::logger>(logger_name, spdlog::sinks_init_list{console, file});
    logger->set_level(spdlog::level::debug);
    logger->flush_on(spdlog::level::debug);   // the monitor sees every line immediately
    spdlog::set_default_logger(logger);
}
