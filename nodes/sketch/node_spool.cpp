#include "node_spool.h"

#include <Arduino.h>
#include <FS.h>
#include <SPIFFS.h>
#include <esp_rom_crc.h>

#include "../../common/protocol.h"

#include <cstddef>

namespace {

constexpr std::uint32_t spool_magic = 0x4C4F4F53;   // "SOOL"
constexpr std::uint16_t spool_version = 1;
constexpr std::size_t payload_size = common::protocol::max_datagram_size;

std::uint32_t crc32(const void* data, std::size_t length) {
    return esp_rom_crc32_le(0, static_cast<const std::uint8_t*>(data), length);
}

bool seekTo(File& file, std::size_t offset) {
    return file.seek(offset, SeekSet);
}

}  // namespace

bool spoolMount() {
    if (!SPIFFS.begin(true)) {   // true: format the partition if it cannot be mounted
        Serial.println("[spool] SPIFFS mount failed: readings will not be buffered");
        return false;
    }
    Serial.printf("[spool] SPIFFS mounted: %u / %u bytes used\n",
                  static_cast<unsigned>(SPIFFS.usedBytes()),
                  static_cast<unsigned>(SPIFFS.totalBytes()));
    return true;
}

Spool::Spool(const char* path, std::uint16_t capacity) : m_path(path), m_capacity(capacity) {}

std::size_t Spool::slotOffset(std::uint16_t index) const {
    return sizeof(Header) + static_cast<std::size_t>(index) * (sizeof(SlotHeader) + payload_size);
}

std::size_t Spool::fileSize() const {
    return slotOffset(m_capacity);
}

std::uint32_t Spool::headerCrc(const Header& header) const {
    return crc32(&header, offsetof(Header, crc));
}

bool Spool::begin() {
    m_ready = false;
    m_header = Header{};

    if (m_capacity == 0) {
        Serial.printf("[spool] %s: capacity must be > 0\n", m_path);
        return false;
    }

    if (!SPIFFS.exists(m_path)) {
        Serial.printf("[spool] %s does not exist, creating\n", m_path);
        return create();
    }

    File file = SPIFFS.open(m_path, FILE_READ);
    if (!file) {
        Serial.printf("[spool] %s: open failed, recreating\n", m_path);
        return create();
    }

    Header header{};
    const bool readOk = file.size() == fileSize() &&
                        file.read(reinterpret_cast<std::uint8_t*>(&header), sizeof(header)) ==
                            sizeof(header);
    file.close();

    const char* problem = nullptr;
    if (!readOk) {
        problem = "unexpected size";
    } else if (header.magic != spool_magic || header.version != spool_version) {
        problem = "unknown format";
    } else if (header.crc != headerCrc(header)) {
        problem = "header checksum mismatch";
    } else if (header.slot_size != payload_size || header.capacity != m_capacity) {
        problem = "geometry changed";
    } else if (header.head >= m_capacity || header.count > m_capacity) {
        problem = "header out of range";
    }

    if (problem != nullptr) {
        Serial.printf("[spool] %s: %s, discarding and recreating\n", m_path, problem);
        return create();
    }

    m_header = header;
    m_ready = true;
    Serial.printf("[spool] %s: %u reading(s) pending from previous run (%u dropped so far)\n",
                  m_path, m_header.count, static_cast<unsigned>(m_header.dropped));
    return true;
}

bool Spool::create() {
    const std::size_t needed = fileSize();
    SPIFFS.remove(m_path);
    const std::size_t freeBytes = SPIFFS.totalBytes() - SPIFFS.usedBytes();
    if (freeBytes < needed) {
        Serial.printf("[spool] %s: not enough SPIFFS space (%u needed, %u free)\n", m_path,
                      static_cast<unsigned>(needed), static_cast<unsigned>(freeBytes));
        return false;
    }

    File file = SPIFFS.open(m_path, FILE_WRITE);
    if (!file) {
        Serial.printf("[spool] %s: create failed\n", m_path);
        return false;
    }

    m_header = Header{};
    m_header.magic = spool_magic;
    m_header.version = spool_version;
    m_header.slot_size = payload_size;
    m_header.capacity = m_capacity;
    m_header.crc = headerCrc(m_header);

    bool ok = file.write(reinterpret_cast<const std::uint8_t*>(&m_header), sizeof(m_header)) ==
              sizeof(m_header);

    // Pre-allocate every slot with zeros (length 0 = empty slot).
    std::uint8_t zeros[256]{};
    std::size_t remaining = needed - sizeof(m_header);
    while (ok && remaining > 0) {
        const std::size_t chunk = remaining < sizeof(zeros) ? remaining : sizeof(zeros);
        ok = file.write(zeros, chunk) == chunk;
        remaining -= chunk;
    }
    file.close();

    if (!ok) {
        Serial.printf("[spool] %s: pre-allocation failed\n", m_path);
        SPIFFS.remove(m_path);
        m_header = Header{};
        return false;
    }

    m_ready = true;
    Serial.printf("[spool] %s: created (%u slots, %u bytes)\n", m_path, m_capacity,
                  static_cast<unsigned>(needed));
    return true;
}

bool Spool::writeHeader() {
    m_header.crc = headerCrc(m_header);
    File file = SPIFFS.open(m_path, "r+");
    if (!file) {
        Serial.printf("[spool] %s: open for header write failed\n", m_path);
        return false;
    }
    const bool ok = seekTo(file, 0) &&
                    file.write(reinterpret_cast<const std::uint8_t*>(&m_header),
                               sizeof(m_header)) == sizeof(m_header);
    file.close();
    if (!ok) {
        Serial.printf("[spool] %s: header write failed\n", m_path);
    }
    return ok;
}

bool Spool::push(const std::string& datagram) {
    if (!m_ready) {
        Serial.println("[spool] not available, reading lost");
        return false;
    }
    if (datagram.empty() || datagram.size() > payload_size) {
        Serial.printf("[spool] %s: reading of %u bytes does not fit a slot, discarded\n",
                      m_path, static_cast<unsigned>(datagram.size()));
        return false;
    }

    const bool full = m_header.count == m_capacity;
    const std::uint16_t index = (m_header.head + m_header.count) % m_capacity;   // == head if full

    SlotHeader slot{};
    slot.length = static_cast<std::uint16_t>(datagram.size());
    slot.crc = crc32(datagram.data(), datagram.size());

    File file = SPIFFS.open(m_path, "r+");
    if (!file) {
        Serial.printf("[spool] %s: open for write failed, reading lost\n", m_path);
        return false;
    }
    const bool ok =
        seekTo(file, slotOffset(index)) &&
        file.write(reinterpret_cast<const std::uint8_t*>(&slot), sizeof(slot)) == sizeof(slot) &&
        file.write(reinterpret_cast<const std::uint8_t*>(datagram.data()), datagram.size()) ==
            datagram.size();
    file.close();

    if (!ok) {
        Serial.printf("[spool] %s: slot write failed, reading lost\n", m_path);
        return false;
    }

    if (full) {
        // Rolling window: the oldest reading was just overwritten.
        m_header.head = (m_header.head + 1) % m_capacity;
        m_header.dropped++;
        Serial.printf("[spool] %s: full, oldest reading overwritten\n", m_path);
    } else {
        m_header.count++;
    }

    if (!writeHeader()) {
        return false;
    }
    Serial.printf("[spool] %s: buffered reading (%u/%u)\n", m_path, m_header.count, m_capacity);
    return true;
}

bool Spool::peek(std::string& datagram) {
    datagram.clear();
    if (!m_ready || m_header.count == 0) {
        return false;
    }

    File file = SPIFFS.open(m_path, FILE_READ);
    if (!file) {
        Serial.printf("[spool] %s: open for read failed\n", m_path);
        return false;
    }

    SlotHeader slot{};
    bool ok = seekTo(file, slotOffset(m_header.head)) &&
              file.read(reinterpret_cast<std::uint8_t*>(&slot), sizeof(slot)) == sizeof(slot) &&
              slot.length > 0 && slot.length <= payload_size;
    if (ok) {
        datagram.resize(slot.length);
        ok = file.read(reinterpret_cast<std::uint8_t*>(&datagram[0]), slot.length) == slot.length;
    }
    file.close();

    if (!ok || crc32(datagram.data(), datagram.size()) != slot.crc) {
        Serial.printf("[spool] %s: slot %u is corrupt\n", m_path, m_header.head);
        datagram.clear();
        return false;
    }
    return true;
}

bool Spool::pop() {
    if (!m_ready || m_header.count == 0) {
        return false;
    }
    m_header.head = (m_header.head + 1) % m_capacity;
    m_header.count--;
    // If this write fails the reading is resent after a restart; both receivers discard
    // duplicates by id, so that is harmless.
    return writeHeader();
}
