if(BUILD_TESTING)
    add_executable(e2e_tests e2e_tests.cpp)
    target_link_libraries(e2e_tests PRIVATE
        alert_server_core fake_udp_client spy_mqtt_broker SQLite::SQLite3 nlohmann_json::nlohmann_json GTest::gtest_main)
    include(GoogleTest)
    gtest_discover_tests(e2e_tests NO_PRETTY_VALUES PROPERTIES TIMEOUT 10)
endif()
