#include "protocol.h"

#include "nlohmann/json.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <limits>
#include <unordered_set>
#include <vector>

namespace common::protocol {
namespace {

using Json = nlohmann::json;

[[noreturn]] void fail(ErrorCode code, const std::string& message) {
    throw ProtocolError(code, message);
}

std::string text(const Json& value, std::size_t maximum, ErrorCode code = ErrorCode::InvalidMessage) {
    const auto& result = value.get_ref<const std::string&>();
    if (result.empty() || result.size() > maximum) {
        fail(code, "String length is outside protocol limits");
    }
    for (unsigned char character : result) {
        if (character < 32 || character == 127) {
            fail(code, "Control characters are not allowed");
        }
    }
    return result;
}

std::string identifier(const Json& value) {
    auto result = text(value, 64);
    if (result.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.")
        != std::string::npos) {
        fail(ErrorCode::InvalidMessage, "Invalid node or session identifier");
    }
    return result;
}

std::uint32_t unsigned_integer(const Json& value) {
    if (!value.is_number_integer() || value < 0 || value > std::numeric_limits<std::uint32_t>::max()) {
        fail(ErrorCode::InvalidMessage, "Expected an unsigned 32-bit integer");
    }
    return value.get<std::uint32_t>();
}

double number(const Json& value) {
    if (!value.is_number() || !std::isfinite(value.get<double>())) {
        fail(ErrorCode::InvalidPayload, "Expected a finite number");
    }
    return value.get<double>();
}

bool valid_timestamp(const std::string& value) {
    if (value.size() < 20 || value.size() > 30 || value.back() != 'Z') {
        return false;
    }
    const std::string format = "dddd-dd-ddTdd:dd:dd";
    for (std::size_t index = 0; index < format.size(); ++index) {
        if (format[index] == 'd' ? value[index] < '0' || value[index] > '9'
                                 : value[index] != format[index]) {
            return false;
        }
    }
    if (value.size() != 20 && (value.size() < 22 || value[19] != '.' ||
        value.substr(20, value.size() - 21).find_first_not_of("0123456789") != std::string::npos)) {
        return false;
    }
    const std::chrono::year_month_day date{
        std::chrono::year{std::stoi(value.substr(0, 4))},
        std::chrono::month{static_cast<unsigned>(std::stoi(value.substr(5, 2)))},
        std::chrono::day{static_cast<unsigned>(std::stoi(value.substr(8, 2)))}};

    return date.year() != std::chrono::year{0} && date.ok() &&
           std::stoi(value.substr(11, 2)) <= 23 &&
           std::stoi(value.substr(14, 2)) <= 59 &&
           std::stoi(value.substr(17, 2)) <= 59;
}

std::string urn(const Json& value, std::string_view prefix) {
    auto result = text(value, 256, ErrorCode::InvalidPayload);
    if (result.compare(0, prefix.size(), prefix) != 0 || result.size() == prefix.size()) {
        fail(ErrorCode::InvalidPayload, "Expected a typed NGSI-LD URN");
    }
    for (unsigned char character : result) {
        if (character <= 32 || character >= 127) {
            fail(ErrorCode::InvalidPayload, "Expected an ASCII URN without whitespace");
        }
    }
    return result;
}

WeatherObserved observation(const Json& payload) {
    constexpr auto invalid = ErrorCode::InvalidPayload;
    try {
        if (payload.at("type") != "WeatherObserved") fail(invalid, "Expected WeatherObserved payload");
        auto timestamp = text(payload.at("dateObserved"), 30, invalid);
        if (!valid_timestamp(timestamp)) fail(invalid, "Expected a valid UTC timestamp ending in Z");
        const auto& location = payload.at("location");
        if (location.at("type") != "Point") fail(invalid, "Expected a GeoJSON Point");
        const auto& coordinates = location.at("coordinates");
        if (!coordinates.is_array() || coordinates.size() != 2) {
            fail(invalid, "Expected longitude and latitude");
        }
        const GeoPoint point{number(coordinates[0]), number(coordinates[1])};
        if (point.longitude < -180 || point.longitude > 180 || point.latitude < -90 || point.latitude > 90) {
            fail(invalid, "Coordinates are outside geographic bounds");
        }
        const double humidity = number(payload.at("relativeHumidity"));
        if (humidity < 0 || humidity > 1) fail(invalid, "relativeHumidity must be a fraction between 0 and 1");
        return {urn(payload.at("id"), "urn:ngsi-ld:WeatherObserved:"), timestamp, point,
                urn(payload.at("refDevice"), "urn:ngsi-ld:Device:"),
                number(payload.at("temperature")), humidity};
    } catch (const Json::exception& error) {
        fail(invalid, error.what());
    }
}

constexpr std::array<const char*, 4> reject_codes{
    "invalid_message", "unsupported_version", "invalid_payload", "identity_conflict"};

std::string reject_code(RejectCode code) {
    const auto index = static_cast<std::size_t>(code);
    if (index < reject_codes.size()) return reject_codes[index];
    fail(ErrorCode::InvalidMessage, "Unknown rejection code");
}

RejectCode reject_code(const Json& value) {
    const auto code = text(value, 32);
    for (std::size_t index = 0; index < reject_codes.size(); ++index) {
        if (code == reject_codes[index]) return static_cast<RejectCode>(index);
    }
    fail(ErrorCode::InvalidMessage, "Unknown rejection code");
}

Message decode_json(const Json& document) {
    try {
        if (unsigned_integer(document.at("version")) != version) {
            fail(ErrorCode::UnsupportedVersion, "Unsupported protocol version");
        }
        const auto kind = text(document.at("kind"), 16);
        Identity identity{identifier(document.at("nodeId")), identifier(document.at("sessionId")),
                          unsigned_integer(document.at("seq"))};
        if (kind == "ack") return Ack{identity};
        if (kind == "reject") {
            return Reject{identity, reject_code(document.at("code")), text(document.at("reason"), 256)};
        }
        if (kind != "reading") fail(ErrorCode::UnsupportedKind, "Unsupported message kind");
        const auto qos = unsigned_integer(document.at("qos"));
        if (qos > 1) fail(ErrorCode::InvalidMessage, "Unsupported QoS");
        const auto synchronized = document.value("timeSynchronized", Json(true));
        if (!synchronized.is_boolean()) fail(ErrorCode::InvalidMessage, "timeSynchronized must be boolean");
        return Reading{identity, static_cast<Qos>(qos), observation(document.value("payload", Json())),
                       synchronized.get<bool>()};
    } catch (const Json::exception& error) {
        fail(ErrorCode::InvalidMessage, error.what());
    }
}

Json envelope(const Identity& identity, const char* kind) {
    return {{"version", version}, {"kind", kind}, {"nodeId", identity.node_id},
            {"sessionId", identity.session_id}, {"seq", identity.seq}};
}

Json to_json(const Message& message) {
    if (const auto* reading = std::get_if<Reading>(&message)) {
        auto document = envelope(reading->identity, "reading");
        const auto& payload = reading->payload;
        document["qos"] = static_cast<int>(reading->qos);
        document["timeSynchronized"] = reading->time_synchronized;
        document["payload"] = {
            {"id", payload.id}, {"type", "WeatherObserved"},
            {"dateObserved", payload.date_observed}, {"refDevice", payload.ref_device},
            {"location", {{"type", "Point"},
                          {"coordinates", {payload.location.longitude, payload.location.latitude}}}},
            {"temperature", payload.temperature}, {"relativeHumidity", payload.relative_humidity}};
        return document;
    }
    if (const auto* ack = std::get_if<Ack>(&message)) return envelope(ack->identity, "ack");
    const auto& reject = std::get<Reject>(message);
    auto document = envelope(reject.identity, "reject");
    document["code"] = reject_code(reject.code);
    document["reason"] = reject.reason;
    return document;
}

}

ProtocolError::ProtocolError(ErrorCode code, const std::string& message)
    : std::runtime_error(message), m_code(code) {}

ErrorCode ProtocolError::code() const noexcept { return m_code; }

Message decode(std::string_view datagram) {
    if (datagram.size() > max_datagram_size) {
        fail(ErrorCode::DatagramTooLarge, "Datagram exceeds 1200 bytes");
    }
    if (datagram.find('\0') != std::string_view::npos) {
        fail(ErrorCode::InvalidJson, "Raw NUL bytes are not allowed in JSON datagrams");
    }
    try {
        std::vector<std::unordered_set<std::string>> object_keys;
        const auto check_keys = [&](int, Json::parse_event_t event, Json& value) {
            if (event == Json::parse_event_t::object_start) {
                object_keys.emplace_back();
            } else if (event == Json::parse_event_t::object_end) {
                object_keys.pop_back();
            } else if (event == Json::parse_event_t::key &&
                       !object_keys.back().insert(value.get<std::string>()).second) {
                fail(ErrorCode::InvalidJson, "Duplicate JSON object key");
            }
            return true;
        };
        return decode_json(Json::parse(datagram.begin(), datagram.end(), check_keys));
    } catch (const Json::exception& error) {
        fail(ErrorCode::InvalidJson, error.what());
    }
}

std::string encode(const Message& message) {
    try {
        const auto document = to_json(message);
        decode_json(document);
        auto datagram = document.dump();
        if (datagram.size() > max_datagram_size) {
            fail(ErrorCode::DatagramTooLarge, "Datagram exceeds 1200 bytes");
        }
        return datagram;
    } catch (const Json::exception& error) {
        fail(ErrorCode::InvalidMessage, error.what());
    }
}

}