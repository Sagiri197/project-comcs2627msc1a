# IBleScanner (ble_scanner.h): SimpleBLE implementation, or a stub when WITH_BLE=OFF.
if(WITH_BLE)
    add_library(ble_scanner src/ble_scanner_simpleble.cpp)
    target_link_libraries(ble_scanner PUBLIC spdlog::spdlog PRIVATE simpleble::simpleble)
else()
    add_library(ble_scanner src/ble_scanner_none.cpp)
    target_link_libraries(ble_scanner PUBLIC spdlog::spdlog)
endif()
target_include_directories(ble_scanner PUBLIC include)
