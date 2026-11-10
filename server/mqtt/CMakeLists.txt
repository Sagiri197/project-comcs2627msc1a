# MQTT publishers (libmosquitto, built from vendor/mosquitto).
add_library(mqtt_publishers
    src/mqtt_publisher.cpp
    src/status_publisher.cpp
    src/alert_publisher.cpp
    src/server_stats.cpp
)
target_include_directories(mqtt_publishers PUBLIC include ../config/include)
target_link_libraries(mqtt_publishers
    PUBLIC spdlog::spdlog alert_logic
    PRIVATE common::protocol libmosquitto_static nlohmann_json::nlohmann_json
)

if(BUILD_TESTING)
    add_executable(mqtt_publisher_tests tests/mqtt_publisher_test.cpp)
    target_link_libraries(mqtt_publisher_tests PRIVATE mqtt_publishers GTest::gtest_main)

    add_executable(server_stats_tests tests/server_stats_test.cpp)
    target_link_libraries(server_stats_tests PRIVATE mqtt_publishers nlohmann_json::nlohmann_json GTest::gtest_main)

    include(GoogleTest)
    gtest_discover_tests(mqtt_publisher_tests NO_PRETTY_VALUES PROPERTIES TIMEOUT 10)
    gtest_discover_tests(server_stats_tests NO_PRETTY_VALUES PROPERTIES TIMEOUT 5)
endif()
