#pragma once

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

inline constexpr char logger_name[] = "alert_server";

inline void initialize_logger() {
    spdlog::set_default_logger(spdlog::stdout_color_mt(logger_name));
    spdlog::set_level(spdlog::level::debug);
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] %v");
}