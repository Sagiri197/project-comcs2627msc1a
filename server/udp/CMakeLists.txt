if(WIN32)
    # Winsock implementation. The rest of the code includes "udp_socket.h", so the Windows header
    # is copied into the build tree under that name (re-copied whenever it changes).
    set(UDP_SOCKET_GENERATED_INCLUDE "${CMAKE_CURRENT_BINARY_DIR}/generated")
    configure_file(include/udp_socket_windows.h "${UDP_SOCKET_GENERATED_INCLUDE}/udp_socket.h" COPYONLY)

    add_library(udp_socket src/udp_socket_windows.cpp)
    target_include_directories(udp_socket
        PUBLIC "${UDP_SOCKET_GENERATED_INCLUDE}"
        PRIVATE include)
    target_compile_definitions(udp_socket PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(udp_socket PUBLIC spdlog::spdlog PRIVATE ws2_32)
else()
    # POSIX sockets (Linux / Raspberry Pi).
    add_library(udp_socket src/udp_socket.cpp)
    target_include_directories(udp_socket PUBLIC include)
    target_link_libraries(udp_socket PUBLIC spdlog::spdlog)
endif()

if(BUILD_TESTING)
    add_executable(udp_socket_tests tests/udp_socket_test.cpp)
    target_link_libraries(udp_socket_tests PRIVATE udp_socket GTest::gtest_main)

    include(GoogleTest)
    gtest_discover_tests(udp_socket_tests NO_PRETTY_VALUES PROPERTIES TIMEOUT 5)

endif()
