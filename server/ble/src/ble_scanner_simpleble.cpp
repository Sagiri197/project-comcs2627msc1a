#include "ble_scanner.h"

#include <simpleble/SimpleBLE.h>
#include <spdlog/spdlog.h>

#include <stdexcept>
#include <utility>

namespace {

class SimpleBleScanner final : public IBleScanner {
public:
    SimpleBleScanner(std::string adapter, std::uint16_t company_id)
        : m_adapter_name(std::move(adapter)), m_company_id(company_id) {}

    ~SimpleBleScanner() override { stop(); }

    void start(Handler handler) override {
        if (!SimpleBLE::Adapter::bluetooth_enabled()) {
            throw std::runtime_error("Bluetooth is not enabled");
        }

        auto adapters = SimpleBLE::Adapter::get_adapters();
        bool found = false;
        for (auto& adapter : adapters) {
            if (m_adapter_name.empty() || adapter.identifier() == m_adapter_name ||
                adapter.address() == m_adapter_name) {
                m_adapter = adapter;
                found = true;
                break;
            }
        }
        if (!found) {
            throw std::runtime_error(m_adapter_name.empty()
                ? std::string("no Bluetooth adapter found")
                : "Bluetooth adapter '" + m_adapter_name + "' not found");
        }

        m_handler = std::move(handler);
        // A node changes its advertisement for every reading: the first one is reported as
        // "found", later ones as "updated".
        auto forward = [this](SimpleBLE::Peripheral peripheral) { report(peripheral); };
        m_adapter.set_callback_on_scan_found(forward);
        m_adapter.set_callback_on_scan_updated(forward);

        m_adapter.scan_start();
        m_scanning = true;
        spdlog::info("BLE scanning on adapter {} [{}]", m_adapter.identifier(), m_adapter.address());
    }

    void stop() override {
        if (!m_scanning) return;
        m_scanning = false;
        try {
            m_adapter.scan_stop();
        } catch (const std::exception& error) {
            spdlog::warn("Stopping BLE scan failed: {}", error.what());
        }
    }

private:
    void report(SimpleBLE::Peripheral& peripheral) {
        try {
            const auto data = peripheral.manufacturer_data();
            const auto entry = data.find(m_company_id);
            if (entry == data.end()) return;
            const std::string payload = entry->second;
            m_handler(payload);
        } catch (const std::exception& error) {
            spdlog::warn("Ignoring BLE advertisement: {}", error.what());
        }
    }

    std::string m_adapter_name;
    std::uint16_t m_company_id;
    SimpleBLE::Adapter m_adapter;
    Handler m_handler;
    bool m_scanning = false;
};

}  // namespace

std::unique_ptr<IBleScanner> make_ble_scanner(const std::string& adapter, std::uint16_t company_id) {
    return std::make_unique<SimpleBleScanner>(adapter, company_id);
}
