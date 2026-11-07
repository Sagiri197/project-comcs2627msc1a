#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>

namespace common::protocol {

inline constexpr std::uint32_t version = 1;
inline constexpr std::size_t max_datagram_size = 1200;

enum class Qos { BestEffort = 0, AtLeastOnce = 1 };
enum class RejectCode { InvalidMessage, UnsupportedVersion, InvalidPayload, IdentityConflict };
enum class ErrorCode {
    InvalidJson, InvalidMessage, UnsupportedVersion, UnsupportedKind,
    InvalidPayload, DatagramTooLarge
};

class ProtocolError : public std::runtime_error {
public:
    ProtocolError(ErrorCode code, const std::string& message);
    ErrorCode code() const noexcept;

private:
    ErrorCode m_code;
};

struct Identity {
    std::string node_id;
    std::string session_id;
    std::uint32_t seq;
};

struct GeoPoint {
    double longitude;
    double latitude;
};

struct WeatherObserved {
    std::string id;
    std::string date_observed;
    GeoPoint location;
    std::string ref_device;
    double temperature;
    double relative_humidity;
};

struct Reading {
    Identity identity;
    Qos qos;
    WeatherObserved payload;
    bool time_synchronized = true;
};

struct Ack {
    Identity identity;
};

struct Reject {
    Identity identity;
    RejectCode code;
    std::string reason;
};

using Message = std::variant<Reading, Ack, Reject>;

Message decode(std::string_view datagram);
std::string encode(const Message& message);

}
