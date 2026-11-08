# IUdpSocket (udp_socket.h) with one implementation per platform; make_udp_socket() returns it.
if(WIN32)
    add_library(udp_socket src/udp_socket_windows.cpp)
    target_compile_definitions(udp_socket PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(udp_socket PUBLIC spdlog::spdlog PRIVATE ws2_32)
else()
    add_library(udp_socket src/udp_socket_posix.cpp)
    target_link_libraries(udp_socket PUBLIC spdlog::spdlog)
endif()
target_include_directories(udp_socket PUBLIC include)

if(BUILD_TESTING)
    add_executable(udp_socket_tests tests/udp_socket_test.cpp)
    target_link_libraries(udp_socket_tests PRIVATE udp_socket GTest::gtest_main)

    include(GoogleTest)
    gtest_discover_tests(udp_socket_tests NO_PRETTY_VALUES PROPERTIES TIMEOUT 5)

endif()
