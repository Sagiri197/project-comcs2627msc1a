#pragma once

// Status rolling window: a fixed-size circular buffer of encoded readings kept in a SPIFFS
// file, so readings survive both a link outage and a restart of the ESP32.
//
// File layout:  [Header][Slot 0][Slot 1]...[Slot capacity-1]
//   Slot = [SlotHeader (len, crc32)][max_datagram_size bytes of payload]
// The file is pre-allocated once, so every write is an in-place overwrite (no growth).
// When the window is full the oldest reading is overwritten (rolling window).
//
// Each slot is written before the header, so a power loss mid-write loses at most the
// reading being written; a torn slot is detected by its CRC and skipped on replay.

#include <cstdint>
#include <string>

namespace node {
// Readings kept per link. At ~4 s per reading this covers ~6-7 minutes of outage.
// Each slot is ~1.2 KB, so two spools use ~240 KB of the SPIFFS partition.
inline constexpr std::uint16_t spool_capacity = 100;
}

// Mounts SPIFFS (formatting it if it cannot be mounted). Call once, before Spool::begin().
bool spoolMount();

class Spool {
public:
    Spool(const char* path, std::uint16_t capacity);

    // Opens the file and validates it; (re)creates it if missing, of the wrong size or
    // corrupt. Readings left by a previous run are kept. Returns false if the spool is
    // unusable (readings will then be lost instead of buffered).
    bool begin();

    // Appends a reading; overwrites the oldest one when full.
    bool push(const std::string& datagram);

    // Reads the oldest reading without removing it. Returns false if the slot is corrupt
    // or unreadable (the caller should pop() it to skip it).
    bool peek(std::string& datagram);

    // Removes the oldest reading.
    bool pop();

    std::uint16_t size() const { return m_header.count; }
    bool empty() const { return m_header.count == 0; }
    bool ready() const { return m_ready; }
    std::uint32_t dropped() const { return m_header.dropped; }
    const char* path() const { return m_path; }

private:
    struct Header {
        std::uint32_t magic;
        std::uint16_t version;
        std::uint16_t slot_size;
        std::uint16_t capacity;
        std::uint16_t head;      // index of the oldest reading
        std::uint16_t count;     // readings stored
        std::uint16_t reserved;
        std::uint32_t dropped;   // readings overwritten because the window was full
        std::uint32_t crc;       // CRC32 of the fields above
    };

    struct SlotHeader {
        std::uint16_t length;
        std::uint16_t reserved;
        std::uint32_t crc;       // CRC32 of the payload
    };

    bool create();
    bool writeHeader();
    std::uint32_t headerCrc(const Header& header) const;
    std::size_t slotOffset(std::uint16_t index) const;
    std::size_t fileSize() const;

    const char* m_path;
    std::uint16_t m_capacity;
    Header m_header{};
    bool m_ready = false;
};
