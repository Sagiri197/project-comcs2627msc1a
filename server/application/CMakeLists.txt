add_library(alert_logic src/application.cpp)
target_include_directories(alert_logic PUBLIC include)
target_link_libraries(alert_logic PUBLIC common::protocol spdlog::spdlog)

# Tests
add_executable(alert_logic_tests tests/application_test.cpp)
target_link_libraries(alert_logic_tests PRIVATE alert_logic gtest gtest_main)
add_test(NAME AlertLogicTests COMMAND alert_logic_tests)
