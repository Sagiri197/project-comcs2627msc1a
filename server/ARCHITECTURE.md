# Alert Server - Differential Calculation Flow

## Complete System Architecture

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                           UDP Network                                       │
│                     (ESP32 Sensor Nodes)                                    │
└──────────────────────┬──────────────────────────────────────────────────────┘
                       │
                       ├── Reading(seq=42, node_id="esp32-1", temp=23.0°C, humidity=55%)
                       ├── Reading(seq=42, node_id="esp32-2", temp=25.0°C, humidity=56%)
                       │
                       ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                         AlertServer (Daemon)                                │
│                                                                              │
│  1. Receive UDP datagram                                                    │
│  2. Decode as common::protocol::Message                                     │
│  3. If Reading: m_logic.addReading(*reading)                                │
│  4. Try process: m_logic.tryProcessSequence(seq)                            │
│  5. Save to DB: m_store.save(*reading)                                      │
│  6. Send ACK if AtLeastOnce QoS                                             │
└──────────────────┬──────────────────────────────────────────────────────────┘
                   │
        ┌──────────┴──────────┐
        ▼                     ▼
┌──────────────────┐  ┌──────────────────────────────────────────────────┐
│  Reading Store   │  │           AlertLogic (Application)              │
│                  │  │                                                  │
│  SQLite DB:      │  │  m_pending_sequences:                           │
│  ┌────────────┐  │  │  42 ──┬─ esp32-1 → Reading(23°C, 55%)           │
│  │ node_id    │  │  │      └─ esp32-2 → Reading(25°C, 56%)            │
│  │ session_id │  │  │                                                  │
│  │ seq        │  │  │  When all N devices report for seq:             │
│  │ longitude  │  │  │                                                  │
│  │ latitude   │  │  │  ▼                                               │
│  │ temp       │  │  │  Calculate differential:                        │
│  │ humidity   │  │  │  - |23.0 - 25.0| = 2.0°C                        │
│  └────────────┘  │  │  - |55 - 56| = 1.0%                             │
│                  │  │                                                  │
│  Immediate       │  │  Check thresholds:                              │
│  transactions    │  │  - 2.0°C == 2.0°C threshold? YES → ALERT        │
│  (PRAGMA         │  │  - 1.0% < 5.0% threshold? OK                    │
│  synchronous)    │  │                                                  │
│                  │  │  Check ranges:                                   │
│                  │  │  - 23°C, 25°C ∈ [0, 50]? YES                     │
│                  │  │  - 55%, 56% ∈ [20, 80]? YES                     │
│                  │  │                                                  │
│                  │  │  ▼ Log with spdlog                              │
│                  │  │  [info] ===== SEQUENCE 42 =====                 │
│                  │  │  [info] [esp32-1] Temp: 23.00°C, Humidity: 55%  │
│                  │  │  [info] [esp32-2] Temp: 25.00°C, Humidity: 56%  │
│                  │  │  [info] Temperature Differential: 2.00°C         │
│                  │  │  [info] Humidity Differential: 1.00%             │
│                  │  │  [warn] ⚠️  ALERT - Sequence 42 ⚠️              │
│                  │  │  [warn]   Temperature differential exceeds       │
│                  │  │                                                  │
│                  │  │  ▼ Store result                                  │
│                  │  │  m_processed_sequences[42] = AccumulatedReadings│
│                  │  │                                                  │
│                  │  │  Return AccumulatedReadings to caller            │
│                  │  │                                                  │
└──────────────────┘  └──────────────────────────────────────────────────┘
```

## Key Design Decisions

### 1. Separation of Concerns

**Persistence** (ReadingStore):
- Saves every reading immediately to SQLite
- Indexed by (node_id, session_id, seq)
- Enables recovery and audit trail

**Processing** (AlertLogic):
- Accumulates readings for differential calculation
- Only processes when all devices report
- Independent of storage

**Daemon** (AlertServer):
- Coordinates between network, processing, and storage
- Handles QoS and acknowledgments

### 2. Buffering Strategy

```cpp
m_pending_sequences: map<uint32_t, map<std::string, Reading>>
```

- One level: sequence number (grouping by time)
- Two levels: node ID (identify which device)
- Automatically cleans up when processed

### 3. Threshold Mechanism

```cpp
struct DifferentialResult {
    double temperature_diff;           // Calculated value
    bool exceeds_temp_threshold;       // 2°C
    bool exceeds_humidity_threshold;   // 5%
    bool out_of_temp_range;            // [0, 50]°C
    bool out_of_humidity_range;        // [20, 80]%
};
```

Separated calculation from decision logic for flexibility.

### 4. Logging Strategy

- **Debug:** Buffer operations (low volume, development only)
- **Info:** Complete sequence summary (always collected)
- **Warn:** Alerts (only when thresholds/ranges violated)

## Example Workflow

```
Sequence 42 arrives:
  esp32-1 → Reading(23.0°C, 55%)
  esp32-2 → Reading(25.0°C, 56%)

Step 1: esp32-1 reading arrives
  addReading() → m_pending_sequences[42][esp32-1] = Reading
  tryProcessSequence(42) → 1/2 devices → return nullopt

Step 2: esp32-2 reading arrives
  addReading() → m_pending_sequences[42][esp32-2] = Reading
  tryProcessSequence(42) → 2/2 devices → process!
  
  calculateDifferential():
    |23.0 - 25.0| = 2.0°C (MATCHES threshold)
    |55 - 56| = 1.0% (under threshold)
  
  log_accumulated_reading()
  log_alert_if_needed() → ALERT (threshold hit)
  
  m_processed_sequences[42] = AccumulatedReadings{
    sequence: 42,
    readings_by_node: {esp32-1, esp32-2},
    differential: {2.0, 1.0, true, false, false, false}
  }
  
  m_pending_sequences.erase(42)
  return AccumulatedReadings
```

## Design Rationale

### Why Accumulation?

Single-reading processing cannot compute differentials because it needs both values. Accumulation solves this elegantly:

- Readings arrive at different times → buffer them
- All devices eventually report the same sequence → process when ready
- Independent per sequence → no interference between measurements

### Why Immediate Persistence?

SQLite with PRAGMA synchronous=FULL ensures:
- Every reading survives power failures
- Auditability for compliance
- Recovery mechanism if processing fails

### Why Separate Logic Objects?

AlertLogic and ReadingStore have different concerns:
- Logic: **stateful**, **in-memory**, **realtime**
- Store: **persistent**, **atomic**, **recoverable**

This separation enables:
- Testing each independently
- Swapping storage backend (SQLite → PostgreSQL)
- Swapping alert logic (different thresholds/algorithms)
