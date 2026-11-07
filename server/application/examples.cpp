// Example usage of the redesigned AlertLogic for multi-device differential calculations
//
// This demonstrates how the AlertServer now accumulates readings from multiple
// ESP32 devices and calculates differentials only when all devices have reported
// for the same sequence number.

#include "alert_server.h"
#include "application.h"
#include "reading_store.h"

#include <iostream>

int main() {
    // Initialize storage and logic
    ReadingStore store{"/tmp/alert_readings.db"};
    AlertLogic logic;
    AlertServer server{store, logic, {"0.0.0.0", 8080}};

    std::cout << "Alert Server with Multi-Device Differential Calculation\n";
    std::cout << "=========================================================\n\n";

    std::cout << "How it works:\n";
    std::cout << "1. ESP32 devices send readings with the same sequence number\n";
    std::cout << "2. AlertServer receives each reading via UDP\n";
    std::cout << "3. AlertLogic.addReading() accumulates them by sequence number\n";
    std::cout << "4. When all N devices report for a sequence, tryProcessSequence() succeeds\n";
    std::cout << "5. Differential is calculated (|device1.value - device2.value|)\n";
    std::cout << "6. Alerts generated if:\n";
    std::cout << "   - Temperature differential > 2°C, OR\n";
    std::cout << "   - Humidity differential > 5%, OR\n";
    std::cout << "   - Any value outside valid range (temp: 0-50°C, humidity: 20-80%)\n";
    std::cout << "7. Results are logged with spdlog and stored in SQLite\n\n";

    std::cout << "AlertLogic API:\n";
    std::cout << "  addReading(Reading)           - Queue a reading for processing\n";
    std::cout << "  tryProcessSequence(uint32_t)  - Try to process when all devices report\n";
    std::cout << "  getPendingSequenceCount()     - Check how many incomplete sequences pending\n";
    std::cout << "  getProcessedReading(uint32_t) - Retrieve results of a processed sequence\n\n";

    std::cout << "Structures:\n";
    std::cout << "  AccumulatedReadings:\n";
    std::cout << "    - sequence: uint32_t\n";
    std::cout << "    - readings_by_node: map<node_id, Reading>\n";
    std::cout << "    - differential: optional<DifferentialResult>\n\n";

    std::cout << "  DifferentialResult:\n";
    std::cout << "    - temperature_diff: double\n";
    std::cout << "    - humidity_diff: double\n";
    std::cout << "    - exceeds_temp_threshold: bool\n";
    std::cout << "    - exceeds_humidity_threshold: bool\n";
    std::cout << "    - out_of_temp_range: bool\n";
    std::cout << "    - out_of_humidity_range: bool\n";

    return 0;
}
