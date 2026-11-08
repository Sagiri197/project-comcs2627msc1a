#pragma once

#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <memory>

inline constexpr char logger_name[] = "alert_server";

// Logs to the terminal and to LOG_PATH (logs/alert_server.log, relative to the directory the
// server is started from). The file is what server/scripts/monitor.sh follows: each line starts
// with the Unix time so the monitor can parse it cheaply.
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
