#pragma once

#include <string>

// BLE fallback link: while WiFi is down the node broadcasts its latest reading in a
// non-connectable advertisement (no pairing, no connection, no ACK). The alert server
// scans for it and deduplicates the repeats. Payload format: src/common/ble_reading.h.
class BleBeacon {
public:
    // Initializes the BLE stack. Call once from setup().
    void begin();

    // Advertises `payload` (common::ble::encodeReading output) until replaced or stopped.
    bool advertise(const std::string& payload);

    // Stops advertising (WiFi is back). Safe to call when not advertising.
    void stop();

    bool advertising() const { return m_advertising; }

private:
    bool m_ready = false;
    bool m_advertising = false;
};
