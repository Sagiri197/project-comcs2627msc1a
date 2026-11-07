# AlertLogic Redesign: Multi-Device Differential Calculations

## Overview

The AlertLogic has been redesigned to solve the critical constraint: **differentials require accumulating ALL readings from ALL ESPs at the same sequence number before comparison**.

The original single-reading processing model (`processReading(Reading) → ProcessedReading`) was incompatible with multi-device comparison. The new accumulation-based model supports buffering readings until all devices report.

## Key Changes

### Architecture

**Before:**
```
Reading → AlertLogic.processReading() → ProcessedReading (immediate) → Storage
```

**After:**
```
Reading → AlertLogic.addReading() → Buffer by sequence
           ↓
       tryProcessSequence() → When all N devices report:
           ↓
       Calculate differential → Log results → Store processed sequence
```

### New AlertLogic Interface

```cpp
class AlertLogic {
public:
    // Accumulation API
    void addReading(const common::protocol::Reading& reading);
    std::optional<AccumulatedReadings> tryProcessSequence(std::uint32_t sequence);
    
    // Query API
    size_t getPendingSequenceCount() const;
    std::optional<AccumulatedReadings> getProcessedReading(std::uint32_t sequence) const;
};
```

### New Data Structures

```cpp
struct DifferentialResult {
    double temperature_diff;           // |device1.temp - device2.temp|
    double humidity_diff;              // |device1.humidity - device2.humidity|
    bool exceeds_temp_threshold;       // > 2°C
    bool exceeds_humidity_threshold;   // > 5%
    bool out_of_temp_range;            // 0-50°C
    bool out_of_humidity_range;        // 20-80%
};

struct AccumulatedReadings {
    std::uint32_t sequence;
    std::map<std::string, Reading> readings_by_node;
    std::optional<DifferentialResult> differential;
};
```

### Thresholds

- **Temperature threshold:** 2°C (alert if differential exceeds)
- **Humidity threshold:** 5% (alert if differential exceeds)
- **Temperature range:** 0-50°C (alert if any device out of range)
- **Humidity range:** 20-80% (alert if any device out of range)
- **Expected devices:** 2 (hardcoded, can be made configurable)

## Implementation Details

### Reading Accumulation

Readings are buffered in a two-level map:
```
m_pending_sequences: map<seq, map<node_id, Reading>>
```

When `tryProcessSequence(seq)` is called:
1. Check if sequence exists
2. Check if we have ≥ EXPECTED_DEVICES readings
3. If yes: calculate differential, log, store in `m_processed_sequences`, return result
4. If no: return `nullopt`

### Differential Calculation

For each pair of readings:
1. Calculate absolute differences: `|payload1.value - payload2.value|`
2. Compare against thresholds (2°C, 5%)
3. Check if values are within valid ranges
4. Return composite result

### Alert Logging

Uses spdlog with three log levels:

1. **Debug:** When reading is added to pending buffer
2. **Info:** Complete sequence summary with readings and differential values
3. **Warning:** Only if alert condition detected (threshold or range violation)

Example output:
```
[info] ===== SEQUENCE 42 =====
[info] [esp32-1] Temperature: 23.00°C, Humidity: 55.00%
[info] [esp32-2] Temperature: 25.00°C, Humidity: 56.00%
[info] Temperature Differential: 2.00°C (threshold: 2.00°C)
[info] Humidity Differential: 1.00% (threshold: 5.00%)
[info] ========================
[warn] ⚠️  ALERT - Sequence 42 ⚠️
[warn]   Temperature differential 2.00°C EXCEEDS threshold 2.00°C
```

## Test Coverage

### AlertLogic Tests (10 tests, all passing)

1. ✅ `AccumulatesReadingsBySequence` - Verifies buffering until complete
2. ✅ `CalculatesTemperatureDifferential` - Correct math
3. ✅ `DetectsExceededTemperatureThreshold` - Alert generation for temp
4. ✅ `DetectsExceededHumidityThreshold` - Alert generation for humidity
5. ✅ `DetectsOutOfRangeTemperature` - Range validation (temperature)
6. ✅ `DetectsOutOfRangeHumidity` - Range validation (humidity)
7. ✅ `AllowsNormalReadings` - No false positives for valid data
8. ✅ `HandlesMultipleSequences` - Independent processing per sequence
9. ✅ `StoresProcessedSequences` - Result retrieval
10. ✅ `ReturnsNulloptForNonexistentSequence` - Error handling

### Persistence Tests (14 tests, all passing)

- Unchanged from before; confirms SQLite persistence working correctly

### Alert Server Tests (30/33 passing)

- 30 tests pass (including integration tests)
- 3 pre-existing failures (protocol encoding, unrelated to this work)

## Integration with AlertServer

The daemon now uses the new API:

```cpp
AlertServer::ReceivedMessage AlertServer::receive() {
    auto datagram = m_socket.recv();
    auto message = common::protocol::decode(datagram.data);

    if (const auto* reading = std::get_if<common::protocol::Reading>(&message)) {
        // Accumulate reading
        m_logic.addReading(*reading);
        
        // Try to process the sequence (succeeds when all ESPs report)
        auto accumulated = m_logic.tryProcessSequence(reading->identity.seq);
        
        // Save the reading (separate from differential calculation)
        const auto result = m_store.save(*reading);

        if (reading->qos == common::protocol::Qos::AtLeastOnce) {
            send(common::protocol::Ack{reading->identity}, datagram.sender);
        }
    }

    return {std::move(message), std::move(datagram.sender)};
}
```

## What's Next

The differential calculation framework is complete. Future work includes:

1. **Timeout handling:** Drop sequences that don't complete within N seconds
2. **MQTT publishing:** Send alerts to command centre via MQTT
3. **Configurable device count:** Make EXPECTED_DEVICES runtime-configurable
4. **Enhanced alert logging:** Write structured alerts to separate log stream
5. **Historical tracking:** Keep differential history for trend analysis

## Files Modified

- `server/application/include/application.h` - Redesigned interface
- `server/application/src/application.cpp` - Accumulation implementation
- `server/application/tests/application_test.cpp` - 10 comprehensive tests
- `server/application/CMakeLists.txt` - Added test targets
- `server/daemon/src/alert_server.cpp` - Integrated new API
