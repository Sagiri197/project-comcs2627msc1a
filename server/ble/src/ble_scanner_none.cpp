#include "ble_scanner.h"

#include <stdexcept>

// Built when WITH_BLE=OFF (no SimpleBLE backend for this platform/toolchain).
std::unique_ptr<IBleScanner> make_ble_scanner(const std::string&, std::uint16_t) {
    throw std::runtime_error("this alert server was built without BLE support (WITH_BLE=OFF)");
}
