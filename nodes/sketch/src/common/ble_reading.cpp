#include "ble_reading.h"

#include <cmath>
#include <cstdio>
#include <limits>

namespace common::ble {
namespace {

constexpr std::uint8_t magic_mask = 0xF0;
constexpr std::uint8_t magic = 0xC0;
constexpr std::uint8_t time_sync_flag = 0x08;
constexpr std::uint8_t version_mask = 0x07;
constexpr std::uint8_t version = 0x01;

constexpr std::size_t session_offset = 1;
constexpr std::size_t seq_offset = 5;
constexpr std::size_t time_offset = 9;
constexpr std::size_t temperature_offset = 13;
constexpr std::size_t humidity_offset = 15;
constexpr std::size_t node_offset = 17;
static_assert(node_offset + max_node_id_size == payload_size);

void put(std::string& out, std::size_t offset, std::uint32_t value, std::size_t bytes) {
    for (std::size_t i = 0; i < bytes; ++i) {
        out[offset + i] = static_cast<char>((value >> (8 * i)) & 0xFF);
    }
}

std::uint32_t get(std::string_view in, std::size_t offset, std::size_t bytes) {
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < bytes; ++i) {
        value |= static_cast<std::uint32_t>(static_cast<unsigned char>(in[offset + i])) << (8 * i);
    }
    return value;
}

// Days since 1970-01-01 for a proleptic Gregorian date (Howard Hinnant's algorithm).
std::int64_t days_from_civil(std::int64_t y, unsigned m, unsigned d) {
    y -= m <= 2;
    const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<std::int64_t>(doe) - 719468;
}

void civil_from_days(std::int64_t z, int& y, unsigned& m, unsigned& d) {
    z += 719468;
    const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    d = doy - (153 * mp + 2) / 5 + 1;
    m = mp < 10 ? mp + 3 : mp - 9;
    y = static_cast<int>(static_cast<std::int64_t>(yoe) + era * 400 + (m <= 2));
}

bool digits(std::string_view text, std::size_t offset, std::size_t count, unsigned& value) {
    value = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const char c = text[offset + i];
        if (c < '0' || c > '9') return false;
        value = value * 10 + static_cast<unsigned>(c - '0');
    }
    return true;
}

// Parses "YYYY-MM-DDTHH:MM:SSZ" (the format the nodes produce) into Unix seconds.
std::optional<std::uint32_t> parse_timestamp(std::string_view text) {
    if (text.size() != 20 || text[4] != '-' || text[7] != '-' || text[10] != 'T' ||
        text[13] != ':' || text[16] != ':' || text[19] != 'Z') {
        return std::nullopt;
    }
    unsigned year, month, day, hour, minute, second;
    if (!digits(text, 0, 4, year) || !digits(text, 5, 2, month) || !digits(text, 8, 2, day) ||
        !digits(text, 11, 2, hour) || !digits(text, 14, 2, minute) || !digits(text, 17, 2, second)) {
        return std::nullopt;
    }
    if (month < 1 || month > 12 || day < 1 || day > 31 || hour > 23 || minute > 59 || second > 60) {
        return std::nullopt;
    }
    const std::int64_t seconds = days_from_civil(year, month, day) * 86400 +
                                 hour * 3600 + minute * 60 + second;
    if (seconds < 0 || seconds > std::numeric_limits<std::uint32_t>::max()) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(seconds);
}

std::string format_timestamp(std::uint32_t epoch) {
    int year;
    unsigned month, day;
    civil_from_days(epoch / 86400, year, month, day);
    const unsigned rest = epoch % 86400;
    char buffer[24]{};
    std::snprintf(buffer, sizeof(buffer), "%04d-%02u-%02uT%02u:%02u:%02uZ",
                  year, month, day, rest / 3600, (rest / 60) % 60, rest % 60);
    return buffer;
}

std::optional<std::uint32_t> parse_session(std::string_view session) {
    if (session.size() <= session_prefix.size() || session.size() > session_prefix.size() + 10 ||
        session.substr(0, session_prefix.size()) != session_prefix) {
        return std::nullopt;
    }
    std::uint64_t value = 0;
    for (char c : session.substr(session_prefix.size())) {
        if (c < '0' || c > '9') return std::nullopt;
        value = value * 10 + static_cast<std::uint64_t>(c - '0');
    }
    if (value > std::numeric_limits<std::uint32_t>::max()) return std::nullopt;
    return static_cast<std::uint32_t>(value);
}

bool valid_node_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
           c == '-' || c == '_' || c == '.';
}

}  // namespace

std::optional<std::string> encodeReading(const common::protocol::Reading& reading) {
    const auto& node_id = reading.identity.node_id;
    if (node_id.empty() || node_id.size() > max_node_id_size) return std::nullopt;
    for (char c : node_id) {
        if (!valid_node_char(c)) return std::nullopt;
    }

    const auto nonce = parse_session(reading.identity.session_id);
    const auto epoch = parse_timestamp(reading.payload.date_observed);
    if (!nonce || !epoch) return std::nullopt;

    const double temperature = std::round(reading.payload.temperature * 100.0);
    const double humidity = std::round(reading.payload.relative_humidity * 10000.0);
    if (!std::isfinite(temperature) || temperature < std::numeric_limits<std::int16_t>::min() ||
        temperature > std::numeric_limits<std::int16_t>::max() ||
        !std::isfinite(humidity) || humidity < 0 || humidity > 10000) {
        return std::nullopt;
    }

    std::string out(payload_size, '\0');
    out[0] = static_cast<char>(magic | version | (reading.time_synchronized ? time_sync_flag : 0));
    put(out, session_offset, *nonce, 4);
    put(out, seq_offset, reading.identity.seq, 4);
    put(out, time_offset, *epoch, 4);
    put(out, temperature_offset,
        static_cast<std::uint16_t>(static_cast<std::int16_t>(temperature)), 2);
    put(out, humidity_offset, static_cast<std::uint16_t>(humidity), 2);
    out.replace(node_offset, node_id.size(), node_id);
    return out;
}

std::optional<common::protocol::Reading> decodeReading(std::string_view payload) {
    if (payload.size() != payload_size) return std::nullopt;

    const auto header = static_cast<std::uint8_t>(payload[0]);
    if ((header & magic_mask) != magic || (header & version_mask) != version) return std::nullopt;

    std::string_view node = payload.substr(node_offset, max_node_id_size);
    node = node.substr(0, node.find('\0'));
    if (node.empty()) return std::nullopt;
    for (char c : node) {
        if (!valid_node_char(c)) return std::nullopt;
    }

    const std::uint16_t humidity = static_cast<std::uint16_t>(get(payload, humidity_offset, 2));
    if (humidity > 10000) return std::nullopt;
    const auto temperature = static_cast<std::int16_t>(get(payload, temperature_offset, 2));

    const std::string node_id(node);
    const std::string session_id = std::string(session_prefix) +
                                   std::to_string(get(payload, session_offset, 4));
    const std::uint32_t seq = get(payload, seq_offset, 4);

    common::protocol::Reading reading;
    reading.identity = {node_id, session_id, seq};
    reading.qos = common::protocol::Qos::BestEffort;
    reading.payload = {
        "urn:ngsi-ld:WeatherObserved:" + node_id + ":" + session_id + ":" + std::to_string(seq),
        format_timestamp(get(payload, time_offset, 4)),
        {0.0, 0.0},
        "urn:ngsi-ld:Device:" + node_id,
        temperature / 100.0,
        humidity / 10000.0
    };
    reading.time_synchronized = (header & time_sync_flag) != 0;
    return reading;
}

}
