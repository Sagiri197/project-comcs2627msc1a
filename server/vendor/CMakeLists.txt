find_package(Threads REQUIRED)
add_library(sqlite3 STATIC sqlite/sqlite3.c)
target_include_directories(sqlite3 PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/sqlite")
target_link_libraries(sqlite3 PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
if(UNIX)
    target_link_libraries(sqlite3 PRIVATE m)
endif()
add_library(SQLite::SQLite3 ALIAS sqlite3)

set(WITH_BROKER OFF CACHE BOOL "" FORCE)
set(WITH_CLIENTS OFF CACHE BOOL "" FORCE)
set(WITH_APPS OFF CACHE BOOL "" FORCE)
set(WITH_PLUGINS OFF CACHE BOOL "" FORCE)
set(WITH_LIB_CPP OFF CACHE BOOL "" FORCE)
set(WITH_CJSON OFF CACHE BOOL "" FORCE)
set(WITH_TLS OFF CACHE BOOL "" FORCE)
set(WITH_STATIC_LIBRARIES ON CACHE BOOL "" FORCE)
set(DOCUMENTATION OFF CACHE BOOL "" FORCE)
if(WIN32)
    # Threading on Windows needs pthreads4w; without it use mosquitto_loop() instead of
    # mosquitto_loop_start().
    set(WITH_THREADING OFF CACHE BOOL "" FORCE)
endif()
add_subdirectory(mosquitto EXCLUDE_FROM_ALL)
foreach(mosquitto_target libmosquitto libmosquitto_static)
    foreach(link_property LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
        get_target_property(mosquitto_links ${mosquitto_target} ${link_property})
        list(REMOVE_ITEM mosquitto_links OpenSSL::SSL)
        set_property(TARGET ${mosquitto_target} PROPERTY ${link_property} "${mosquitto_links}")
    endforeach()
endforeach()
target_include_directories(libmosquitto_static PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/mosquitto/include"
)

add_subdirectory(spdlog)

# BLE scanning (SimpleBLE: BlueZ over D-Bus on Linux, WinRT on Windows/MSVC).
if(WITH_BLE)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/SimpleBLE/simpleble/CMakeLists.txt")
        message(FATAL_ERROR
            "SimpleBLE submodule is missing. Run:\n"
            "  git submodule update --init --recursive server/vendor/SimpleBLE\n"
            "or configure with -DWITH_BLE=OFF to build without the BLE fallback.")
    endif()
    set(SIMPLEBLE_INSTALL OFF CACHE BOOL "" FORCE)
    # SimpleBLE compiles its own header-only fmt; on MSVC a static simpleble.lib then clashes
    # with spdlog's bundled fmt at link time (LNK2005). A DLL keeps both copies private.
    if(MSVC)
        set(_saved_build_shared_libs ${BUILD_SHARED_LIBS})
        set(BUILD_SHARED_LIBS ON)
    endif()
    add_subdirectory(SimpleBLE/simpleble EXCLUDE_FROM_ALL)
    if(MSVC)
        set(BUILD_SHARED_LIBS ${_saved_build_shared_libs})
    endif()
endif()
