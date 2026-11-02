#include "common/protocol.h"

#include <nlohmann/json.hpp>

#include <array>
#include <cmath>
#include <limits>
#include <type_traits>
#include <unordered_set>
#include <vector>

namespace common::protocol {
namespace {

using Json = nlohmann::json;

[[noreturn]] void fail(ErrorCode code, const std::string& message) {
    throw ProtocolError(code, message);
}

const Json& field(const Json& object, const char* name, ErrorCode code) {
    if (!object.is_object() || !object.contains(name)) {
        fail(code, std::string("Missing field: ") + name);
    }
    return object.at(name);
}

std::string text(const Json& value, std::size_t maximum, ErrorCode code) {
    if (!value.is_string()) {
        fail(code, "Expected a string");
    }
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
    auto result = text(value, 64, ErrorCode::InvalidMessage);
    for (unsigned char character : result) {
        if (!((character >= 'a' && character <= 'z') ||
              (character >= 'A' && character <= 'Z') ||
              (character >= '0' && character <= '9') ||
              character == '-' || character == '_' || character == '.')) {
            fail(ErrorCode::InvalidMessage, "Invalid node or session identifier");
        }
    }
    return result;
}

std::uint32_t unsigned_integer(const Json& value) {
    if (!value.is_number_integer() ||
        (!value.is_number_unsigned() && value.get<std::int64_t>() < 0) ||
        value.get<std::uint64_t>() > std::numeric_limits<std::uint32_t>::max()) {
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
    if (value.size() < 20 || value[4] != '-' || value[7] != '-' ||
        value[10] != 'T' || value[13] != ':' || value[16] != ':' || value.back() != 'Z') {
        return false;
    }
    if (value.size() != 20 &&
        (value.size() < 22 || value.size() > 30 || value[19] != '.')) {
        return false;
    }
    for (std::size_t index = 0; index < value.size() - 1; ++index) {
        if (index == 4 || index == 7 || index == 10 || index == 13 || index == 16 ||
            (index == 19 && value.size() != 20)) {
            continue;
        }
        if (value[index] < '0' || value[index] > '9') {
            return false;
        }
    }
    const int year = std::stoi(value.substr(0, 4));
    const int month = std::stoi(value.substr(5, 2));
    const int day = std::stoi(value.substr(8, 2));
    if (year == 0 || month < 1 || month > 12) {
        return false;
    }
    const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    const std::array<int, 12> days{31, leap ? 29 : 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return day >= 1 && day <= days[month - 1] &&
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
    if (field(payload, "type", invalid) != "WeatherObserved") {
        fail(invalid, "Expected WeatherObserved payload");
    }
    auto timestamp = text(field(payload, "dateObserved", invalid), 30, invalid);
    if (!valid_timestamp(timestamp)) {
        fail(invalid, "Expected a valid UTC timestamp ending in Z");
    }
    const auto& location = field(payload, "location", invalid);
    if (field(location, "type", invalid) != "Point") {
        fail(invalid, "Expected a GeoJSON Point");
    }
    const auto& coordinates = field(location, "coordinates", invalid);
    if (!coordinates.is_array() || coordinates.size() != 2) {
        fail(invalid, "Expected longitude and latitude");
    }
    const GeoPoint point{number(coordinates[0]), number(coordinates[1])};
    if (point.longitude < -180 || point.longitude > 180 ||
        point.latitude < -90 || point.latitude > 90) {
        fail(invalid, "Coordinates are outside geographic bounds");
    }
    const double humidity = number(field(payload, "relativeHumidity", invalid));
    if (humidity < 0 || humidity > 1) {
        fail(invalid, "relativeHumidity must be a fraction between 0 and 1");
    }
    return {urn(field(payload, "id", invalid), "urn:ngsi-ld:WeatherObserved:"),
            timestamp, point,
            urn(field(payload, "refDevice", invalid), "urn:ngsi-ld:Device:"),
            number(field(payload, "temperature", invalid)), humidity};
}

std::string reject_code(RejectCode code) {
    switch (code) {
    case RejectCode::InvalidMessage: return "invalid_message";
    case RejectCode::UnsupportedVersion: return "unsupported_version";
    case RejectCode::InvalidPayload: return "invalid_payload";
    case RejectCode::IdentityConflict: return "identity_conflict";
    }
    fail(ErrorCode::InvalidMessage, "Unknown rejection code");
}

RejectCode reject_code(const Json& value) {
    const auto code = text(value, 32, ErrorCode::InvalidMessage);
    if (code == "invalid_message") return RejectCode::InvalidMessage;
    if (code == "unsupported_version") return RejectCode::UnsupportedVersion;
    if (code == "invalid_payload") return RejectCode::InvalidPayload;
    if (code == "identity_conflict") return RejectCode::IdentityConflict;
    fail(ErrorCode::InvalidMessage, "Unknown rejection code");
}

Message decode_json(const Json& document) {
    constexpr auto invalid = ErrorCode::InvalidMessage;
    if (unsigned_integer(field(document, "version", invalid)) != version) {
        fail(ErrorCode::UnsupportedVersion, "Unsupported protocol version");
    }
    const auto kind = text(field(document, "kind", invalid), 16, invalid);
    Identity identity{identifier(field(document, "nodeId", invalid)),
                      identifier(field(document, "sessionId", invalid)),
                      unsigned_integer(field(document, "seq", invalid))};
    if (kind == "ack") return Ack{identity};
    if (kind == "reject") {
        return Reject{identity, reject_code(field(document, "code", invalid)),
                      text(field(document, "reason", invalid), 256, invalid)};
    }
    if (kind != "reading") {
        fail(ErrorCode::UnsupportedKind, "Unsupported message kind");
    }
    const auto qos = unsigned_integer(field(document, "qos", invalid));
    if (qos > 1) fail(invalid, "Unsupported QoS");
    bool synchronized = true;
    if (document.contains("timeSynchronized")) {
        const auto& value = document.at("timeSynchronized");
        if (!value.is_boolean()) fail(invalid, "timeSynchronized must be boolean");
        synchronized = value.get<bool>();
    }
    return Reading{identity, static_cast<Qos>(qos),
                   observation(field(document, "payload", ErrorCode::InvalidPayload)), synchronized};
}

Json to_json(const Message& message) {
    return std::visit([](const auto& typed) {
        Json document{{"version", version}, {"nodeId", typed.identity.node_id},
                      {"sessionId", typed.identity.session_id}, {"seq", typed.identity.seq}};
        using Type = std::decay_t<decltype(typed)>;
        if constexpr (std::is_same_v<Type, Reading>) {
            const auto& payload = typed.payload;
            document["kind"] = "reading";
            document["qos"] = static_cast<int>(typed.qos);
            document["timeSynchronized"] = typed.time_synchronized;
            document["payload"] = {
                {"id", payload.id}, {"type", "WeatherObserved"},
                {"dateObserved", payload.date_observed}, {"refDevice", payload.ref_device},
                {"location", {{"type", "Point"},
                              {"coordinates", {payload.location.longitude, payload.location.latitude}}}},
                {"temperature", payload.temperature}, {"relativeHumidity", payload.relative_humidity}};
        } else if constexpr (std::is_same_v<Type, Ack>) {
            document["kind"] = "ack";
        } else {
            document["kind"] = "reject";
            document["code"] = reject_code(typed.code);
            document["reason"] = typed.reason;
        }
        return document;
    }, message);
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