add_executable(
    alert_server
    src/main.cpp
)

target_include_directories(alert_server PRIVATE include)
target_link_libraries(alert_server PRIVATE spdlog::spdlog udp_socket)