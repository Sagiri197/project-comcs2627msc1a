cmake_minimum_required(VERSION 3.18)

project(alert_server VERSION 1.0 LANGUAGES C CXX)

include(CTest)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Constants
add_compile_definitions(
    LOGGER_NAME="alert_server"
    LOG_PATH="logs/alert_server.log"
)

# Doxygen
find_package(Doxygen)

if(DOXYGEN_FOUND)
    add_custom_target(doxygen
        COMMAND ${DOXYGEN_EXECUTABLE}
        ${CMAKE_SOURCE_DIR}/Doxyfile
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Generating API documentation"
        VERBATIM
    )
endif()

add_subdirectory(vendor)

# The unit tests use POSIX sockets/unistd directly; the server itself builds on Windows.
if(WIN32 AND BUILD_TESTING)
    message(STATUS "Unit tests are POSIX-only: skipped on Windows")
    set(BUILD_TESTING OFF)
endif()

if(BUILD_TESTING)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vendor/googletest/CMakeLists.txt")
        message(FATAL_ERROR "GoogleTest submodule is missing")
    endif()
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
    add_subdirectory(vendor/googletest)
endif()

set(SKETCH_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../nodes/sketch/src")

add_library(nlohmann_json INTERFACE)
add_library(nlohmann_json::nlohmann_json ALIAS nlohmann_json)
target_include_directories(nlohmann_json INTERFACE "${SKETCH_SRC}/common")

add_library(common_protocol "${SKETCH_SRC}/common/protocol.cpp")
add_library(common::protocol ALIAS common_protocol)
target_include_directories(common_protocol PUBLIC "${SKETCH_SRC}" "${SKETCH_SRC}/common")
target_compile_features(common_protocol PUBLIC cxx_std_20)
target_link_libraries(common_protocol PRIVATE nlohmann_json::nlohmann_json)

if(BUILD_TESTING)
    add_executable(protocol_tests protocol_test.cpp)
    target_link_libraries(protocol_tests PRIVATE common::protocol nlohmann_json::nlohmann_json GTest::gtest_main)
    include(GoogleTest)
    gtest_discover_tests(protocol_tests PROPERTIES TIMEOUT 5)
endif()

add_subdirectory(tools)
add_subdirectory(udp)
add_subdirectory(persistency)
add_subdirectory(application)
add_subdirectory(mqtt)
add_subdirectory(daemon)

if(BUILD_TESTING)
    add_subdirectory(e2e)
endif()
