#pragma once

#include <cstdint>
#include <string>

namespace node {
/** @brief Number of readings retained in each per-link spool. */
inline constexpr std::uint16_t spool_capacity = 100;
}

/**
 * @brief Mounts SPIFFS, formatting the partition if mounting fails.
 * @return `true` if SPIFFS mounted successfully.
 * @note Call before `Spool::begin()`.
 */
bool spoolMount();

/**
 * @brief Fixed-capacity circular buffer of encoded readings stored in SPIFFS.
 *
 * The file contains a header followed by one fixed-size slot per capacity.
 * Each slot stores a datagram and its CRC; a full spool overwrites its oldest
 * reading.
 */
class Spool {
public:
    /**
     * @brief Creates a spool handle.
     * @param path SPIFFS file path for the spool.
     * @param capacity Number of readings the spool can hold.
     */
    Spool(const char* path, std::uint16_t capacity);

    /**
     * @brief Opens and validates the spool, creating or recreating it if necessary.
     *
     * Existing readings are retained when the file is valid. An invalid or
     * incorrectly sized file is discarded and recreated.
     * @return `true` if the spool is ready for use.
     */
    bool begin();

    /**
     * @brief Appends a reading, overwriting the oldest reading if the spool is full.
     * @param datagram Encoded reading to store.
     * @return `true` if the reading was written successfully.
     */
    bool push(const std::string& datagram);

    /**
     * @brief Reads the oldest reading without removing it.
     * @param[out] datagram Receives the encoded reading.
     * @return `true` if a valid reading was read; otherwise `false`.
     * @note A corrupt or unreadable slot can be skipped with `pop()`.
     */
    bool peek(std::string& datagram);

    /**
     * @brief Removes the oldest reading.
     * @return `true` if a reading was removed successfully.
     */
    bool pop();

    /** @brief Returns the number of readings currently stored. */
    std::uint16_t size() const { return m_header.count; }

    /** @brief Returns whether the spool contains no readings. */
    bool empty() const { return m_header.count == 0; }

    /** @brief Returns whether the spool is ready for use. */
    bool ready() const { return m_ready; }

    /** @brief Returns the number of readings overwritten when the spool was full. */
    std::uint32_t dropped() const { return m_header.dropped; }

    /** @brief Returns the SPIFFS path used by this spool. */
    const char* path() const { return m_path; }

private:
    struct Header {
        std::uint32_t magic;
        std::uint16_t version;
        std::uint16_t slot_size;
        std::uint16_t capacity;
        std::uint16_t head;      ///< Index of the oldest reading.
        std::uint16_t count;     ///< Number of readings stored.
        std::uint16_t reserved;
        std::uint32_t dropped;   ///< Readings overwritten because the spool was full.
        std::uint32_t crc;       ///< CRC32 of the preceding header fields.
    };

    struct SlotHeader {
        std::uint16_t length;
        std::uint16_t reserved;
        std::uint32_t crc;       ///< CRC32 of the payload.
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
