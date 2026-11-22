#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

/**
 * @brief Passive listener for BLE advertisements (no pairing, no connection).
 *
 * Nodes that lost WiFi broadcast their readings as Manufacturer Specific Data; the scanner
 * hands every matching advertisement to a callback. The callback runs on the BLE stack's
 * own thread and is called repeatedly for the same advertisement, so the receiver must
 * deduplicate.
 */
class IBleScanner {
public:
    /** @brief Receives the manufacturer data that follows the company identifier. */
    using Handler = std::function<void(std::string_view manufacturer_data)>;

    virtual ~IBleScanner() = default;

    /**
     * @brief Starts scanning continuously in the background.
     * @throws std::runtime_error if no usable Bluetooth adapter is available.
     */
    virtual void start(Handler handler) = 0;

    /** @brief Stops scanning; safe to call when not scanning. */
    virtual void stop() = 0;

protected:
    IBleScanner() = default;
    IBleScanner(const IBleScanner&) = delete;
    IBleScanner& operator=(const IBleScanner&) = delete;
};

/**
 * @brief Creates the platform BLE scanner.
 * @param adapter Adapter identifier (e.g. "hci0") or address; empty selects the first one.
 * @param company_id Only manufacturer data with this company identifier is reported.
 * @throws std::runtime_error if the server was built without BLE support (WITH_BLE=OFF).
 */
std::unique_ptr<IBleScanner> make_ble_scanner(const std::string& adapter, std::uint16_t company_id);
