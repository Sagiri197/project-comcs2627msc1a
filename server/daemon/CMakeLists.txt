add_library(alert_server_core src/alert_server.cpp src/server_stats.cpp src/alert_message.cpp)
target_include_directories(alert_server_core PUBLIC include)
target_link_libraries(alert_server_core PUBLIC udp_socket common::protocol persistency alert_logic
    PRIVATE nlohmann_json::nlohmann_json)

add_executable(
    alert_server
    src/main.cpp
)

target_include_directories(alert_server PRIVATE include)
target_link_libraries(alert_server PRIVATE
    spdlog::spdlog
    alert_server_core
    mqtt_status
    nlohmann_json::nlohmann_json
)

if(BUILD_TESTING)
    add_executable(alert_server_tests tests/alert_server_test.cpp)
    target_link_libraries(alert_server_tests PRIVATE
        alert_server_core SQLite::SQLite3 nlohmann_json::nlohmann_json GTest::gtest_main)
    include(GoogleTest)
    gtest_discover_tests(alert_server_tests NO_PRETTY_VALUES PROPERTIES TIMEOUT 5)

    add_executable(server_stats_tests tests/server_stats_test.cpp)
    target_link_libraries(server_stats_tests PRIVATE
        alert_server_core nlohmann_json::nlohmann_json GTest::gtest_main)
    gtest_discover_tests(server_stats_tests NO_PRETTY_VALUES PROPERTIES TIMEOUT 5)

    add_executable(alert_message_tests tests/alert_message_test.cpp)
    target_link_libraries(alert_message_tests PRIVATE
        alert_server_core nlohmann_json::nlohmann_json GTest::gtest_main)
    gtest_discover_tests(alert_message_tests NO_PRETTY_VALUES PROPERTIES TIMEOUT 5)
endif()