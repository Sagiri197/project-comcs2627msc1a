add_library(
    udp_socket
    src/udp_socket.cpp
)

target_include_directories(udp_socket PUBLIC include)
target_link_libraries(udp_socket PUBLIC spdlog::spdlog)

if(BUILD_TESTING)
    add_executable(udp_socket_tests tests/udp_socket_test.cpp)
    target_link_libraries(udp_socket_tests PRIVATE udp_socket GTest::gtest_main)

    include(GoogleTest)
    gtest_discover_tests(udp_socket_tests NO_PRETTY_VALUES PROPERTIES TIMEOUT 5)

endif()