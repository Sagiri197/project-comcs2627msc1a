# MQTT status/heartbeat publisher (libmosquitto, built from vendor/mosquitto).
add_library(mqtt_status src/status_publisher.cpp)
target_include_directories(mqtt_status PUBLIC include)
target_link_libraries(mqtt_status PUBLIC spdlog::spdlog PRIVATE libmosquitto_static)

if(BUILD_TESTING)
    add_executable(mqtt_status_tests tests/status_publisher_test.cpp)
    target_link_libraries(mqtt_status_tests PRIVATE mqtt_status GTest::gtest_main)

    include(GoogleTest)
    gtest_discover_tests(mqtt_status_tests NO_PRETTY_VALUES PROPERTIES TIMEOUT 10)
endif()
