#include "node_ble.h"

#include "../common/ble_reading.h"
#include "../config.h"

#include <Arduino.h>
#include <BLEAdvertising.h>
#include <BLEDevice.h>

#include <cstring>
#include <string>

static_assert(std::char_traits<char>::length(node::config.node_id) <= common::ble::max_node_id_size,
              "node_id does not fit in the BLE advertisement");

namespace {

// Advertising interval in 0.625 ms units: 100-150 ms gives a scanner many chances per reading.
constexpr std::uint16_t min_interval = 160;
constexpr std::uint16_t max_interval = 240;

#if defined(CONFIG_BLUEDROID_ENABLED)
constexpr std::uint8_t non_connectable = ADV_TYPE_NONCONN_IND;
#else
constexpr std::uint8_t non_connectable = BLE_GAP_CONN_MODE_NON;
#endif

}  // namespace

void BleBeacon::begin() {
    if (m_ready) return;
    BLEDevice::init(node::config.node_id);
    BLEAdvertising* advertising = BLEDevice::getAdvertising();
    advertising->setAdvertisementType(non_connectable);
    advertising->setScanResponse(false);
    advertising->setMinInterval(min_interval);
    advertising->setMaxInterval(max_interval);
    m_ready = true;
    Serial.println("[ble] initialized");
}

bool BleBeacon::advertise(const std::string& payload) {
    if (!m_ready || payload.size() != common::ble::payload_size) {
        return false;
    }

    // One Manufacturer Specific Data AD structure: [len][0xFF][company LE][payload].
    char structure[4 + common::ble::payload_size];
    structure[0] = static_cast<char>(sizeof(structure) - 1);
    structure[1] = static_cast<char>(0xFF);
    structure[2] = static_cast<char>(common::ble::company_id & 0xFF);
    structure[3] = static_cast<char>(common::ble::company_id >> 8);
    std::memcpy(structure + 4, payload.data(), payload.size());

    BLEAdvertisementData data;
    data.addData(structure, sizeof(structure));

    BLEAdvertising* advertising = BLEDevice::getAdvertising();
    if (m_advertising) {
        advertising->stop();
    }
    if (!advertising->setAdvertisementData(data)) {
        Serial.println("[ble] could not set advertisement data");
        m_advertising = false;
        return false;
    }
    m_advertising = advertising->start();
    Serial.println(m_advertising ? "[ble] advertising reading" : "[ble] advertising failed");
    return m_advertising;
}

void BleBeacon::stop() {
    if (!m_advertising) return;
    BLEDevice::getAdvertising()->stop();
    m_advertising = false;
    Serial.println("[ble] WiFi back: advertising stopped");
}
