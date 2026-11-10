#ifndef ARDUINO
#include "protocol.h"
#include "mqtt_topics.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <limits>

namespace {

using namespace common::protocol;
using Json = nlohmann::json;

TEST(MqttTopics, PreservesNodeTopics) {
    EXPECT_EQ(common::mqtt::weather_topic("esp32-003"), "comcs/cell1/esp32-003/weather");
    EXPECT_EQ(common::mqtt::status_topic(common::mqtt::node_topic("esp32-003")),
              "comcs/cell1/esp32-003/status");
}

TEST(MqttTopics, PreservesConfigurableServerBase) {
    EXPECT_EQ(common::mqtt::status_topic("custom/alert-server"), "custom/alert-server/status");
    EXPECT_EQ(common::mqtt::heartbeat_topic("custom/alert-server"), "custom/alert-server/heartbeat");
}

Reading reading() {
    return {{"esp32-a", "c79e4fd8-5888-4ae5-8556-c1e558aa6020", 42}, Qos::AtLeastOnce,
            {"urn:ngsi-ld:WeatherObserved:esp32-a:session:42", "2026-11-02T14:30:01Z",
             {-8.608, 41.178}, "urn:ngsi-ld:Device:esp32-a", 23.4, 0.56}};
}

void expect_error(const std::string& datagram, ErrorCode code) {
    try {
        decode(datagram);
        FAIL() << "Expected ProtocolError";
    } catch (const ProtocolError& error) {
        EXPECT_EQ(error.code(), code) << error.what();
        EXPECT_FALSE(std::string(error.what()).empty());
    }
}

TEST(Protocol, ReadingRoundTripPreservesAllFields) {
    const auto original = reading();
    const auto wire = encode(original);
    ASSERT_LE(wire.size(), max_datagram_size);
    const auto decoded = std::get<Reading>(decode(wire));
    EXPECT_EQ(decoded.identity.node_id, original.identity.node_id);
    EXPECT_EQ(decoded.identity.session_id, original.identity.session_id);
    EXPECT_EQ(decoded.identity.seq, 42);
    EXPECT_EQ(decoded.qos, Qos::AtLeastOnce);
    EXPECT_EQ(decoded.payload.id, original.payload.id);
    EXPECT_EQ(decoded.payload.date_observed, original.payload.date_observed);
    EXPECT_EQ(decoded.payload.ref_device, original.payload.ref_device);
    EXPECT_DOUBLE_EQ(decoded.payload.location.longitude, -8.608);
    EXPECT_DOUBLE_EQ(decoded.payload.location.latitude, 41.178);
    EXPECT_DOUBLE_EQ(decoded.payload.temperature, 23.4);
    EXPECT_DOUBLE_EQ(decoded.payload.relative_humidity, 0.56);
    EXPECT_TRUE(decoded.time_synchronized);
    EXPECT_EQ(encode(decoded), wire);
}

TEST(Protocol, SupportsBestEffortAndUnsynchronizedTime) {
    auto original = reading();
    original.qos = Qos::BestEffort;
    original.time_synchronized = false;
    const auto decoded = std::get<Reading>(decode(encode(original)));
    EXPECT_EQ(decoded.qos, Qos::BestEffort);
    EXPECT_FALSE(decoded.time_synchronized);
}

TEST(Protocol, OptionalTimeSynchronizationDefaultsToTrue) {
    auto document = Json::parse(encode(reading()));
    document.erase("timeSynchronized");
    EXPECT_TRUE(std::get<Reading>(decode(document.dump())).time_synchronized);
}

TEST(Protocol, AckRoundTripPreservesCorrelationAndHasNoPayload) {
    const Ack original{reading().identity};
    const auto wire = encode(original);
    const auto decoded = std::get<Ack>(decode(wire));
    EXPECT_EQ(decoded.identity.node_id, original.identity.node_id);
    EXPECT_EQ(decoded.identity.session_id, original.identity.session_id);
    EXPECT_EQ(decoded.identity.seq, original.identity.seq);
    EXPECT_FALSE(Json::parse(wire).contains("payload"));
    EXPECT_EQ(encode(decoded), wire);
}

class RejectRoundTrip : public testing::TestWithParam<RejectCode> {};

TEST_P(RejectRoundTrip, PreservesCodeReasonAndIdentity) {
    const Reject original{reading().identity, GetParam(), "Cannot accept this message"};
    const auto wire = encode(original);
    const auto decoded = std::get<Reject>(decode(wire));
    EXPECT_EQ(decoded.code, GetParam());
    EXPECT_EQ(decoded.reason, original.reason);
    EXPECT_EQ(decoded.identity.node_id, original.identity.node_id);
    EXPECT_EQ(decoded.identity.session_id, original.identity.session_id);
    EXPECT_EQ(decoded.identity.seq, original.identity.seq);
    EXPECT_EQ(encode(decoded), wire);
}

INSTANTIATE_TEST_SUITE_P(Codes, RejectRoundTrip, testing::Values(
    RejectCode::InvalidMessage, RejectCode::UnsupportedVersion,
    RejectCode::InvalidPayload, RejectCode::IdentityConflict));

class InvalidJson : public testing::TestWithParam<std::string> {};

TEST_P(InvalidJson, ReportsParseFailure) {
    expect_error(GetParam(), ErrorCode::InvalidJson);
}

INSTANTIATE_TEST_SUITE_P(Datagrams, InvalidJson, testing::Values(
    "", "{", "not json", "{}{}", "{\"version\":NaN}", std::string("{}\0", 3),
    "{\"version\":1e999}", std::string("{\"kind\":\"\xff\"}")));

class RequiredEnvelopeField : public testing::TestWithParam<const char*> {};

TEST_P(RequiredEnvelopeField, RejectsMissingFields) {
    auto document = Json::parse(encode(reading()));
    document.erase(GetParam());
    expect_error(document.dump(), ErrorCode::InvalidMessage);
}

INSTANTIATE_TEST_SUITE_P(Envelope, RequiredEnvelopeField,
    testing::Values("version", "kind", "nodeId", "sessionId", "seq", "qos"));

class RequiredObservationField : public testing::TestWithParam<const char*> {};

TEST_P(RequiredObservationField, RejectsMissingFields) {
    auto document = Json::parse(encode(reading()));
    document["payload"].erase(GetParam());
    expect_error(document.dump(), ErrorCode::InvalidPayload);
}

INSTANTIATE_TEST_SUITE_P(Observation, RequiredObservationField, testing::Values(
    "id", "type", "dateObserved", "location", "refDevice", "temperature", "relativeHumidity"));

TEST(Protocol, RejectsMissingPayload) {
    auto document = Json::parse(encode(reading()));
    document.erase("payload");
    expect_error(document.dump(), ErrorCode::InvalidPayload);
}

class InvalidEnvelopeValue : public testing::TestWithParam<Json> {};

TEST_P(InvalidEnvelopeValue, RejectsWrongTypesAndValues) {
    auto document = Json::parse(encode(reading()));
    const auto patch = GetParam();
    document.update(patch);
    expect_error(document.dump(), ErrorCode::InvalidMessage);
}

INSTANTIATE_TEST_SUITE_P(EnvelopeValues, InvalidEnvelopeValue, testing::Values(
    Json{{"version", "1"}}, Json{{"version", 1.0}}, Json{{"version", nullptr}},
    Json{{"nodeId", ""}}, Json{{"nodeId", "node space"}}, Json{{"nodeId", 1}},
    Json{{"nodeId", std::string(65, 'a')}}, Json{{"sessionId", ""}},
    Json{{"sessionId", "bad/session"}}, Json{{"sessionId", std::string(65, 'a')}},
    Json{{"seq", -1}}, Json{{"seq", 1.5}}, Json{{"seq", true}},
    Json{{"seq", 4294967296ULL}}, Json{{"seq", "42"}},
    Json{{"seq", std::numeric_limits<std::uint64_t>::max()}},
    Json{{"seq", std::numeric_limits<std::int64_t>::min()}},
    Json{{"seq", Json::array()}}, Json{{"seq", nullptr}},
    Json{{"qos", 2}}, Json{{"qos", -1}}, Json{{"qos", "1"}},
    Json{{"timeSynchronized", 1}}, Json{{"kind", nullptr}},
    Json{{"nodeId", "node\n"}}, Json{{"nodeId", std::string("node\0id", 7)}}));

TEST(Protocol, RejectsUnsupportedVersionAndKind) {
    auto document = Json::parse(encode(reading()));
    document["version"] = 2;
    expect_error(document.dump(), ErrorCode::UnsupportedVersion);
    document["version"] = version;
    document["kind"] = "unknown";
    expect_error(document.dump(), ErrorCode::UnsupportedKind);
}

TEST(Protocol, RejectsNonObjectEnvelopes) {
    for (const auto* wire : {"null", "[]", "42", "true", "\"reading\""}) {
        expect_error(wire, ErrorCode::InvalidMessage);
    }
}

class InvalidPayloadValue : public testing::TestWithParam<Json> {};

TEST_P(InvalidPayloadValue, RejectsWrongTypesAndValues) {
    auto document = Json::parse(encode(reading()));
    document["payload"].update(GetParam());
    expect_error(document.dump(), ErrorCode::InvalidPayload);
}

INSTANTIATE_TEST_SUITE_P(PayloadValues, InvalidPayloadValue, testing::Values(
    Json{{"type", "Device"}}, Json{{"id", ""}}, Json{{"id", "arbitrary"}},
    Json{{"id", "urn:ngsi-ld:WeatherObserved:"}}, Json{{"refDevice", "esp32-a"}},
    Json{{"id", "urn:ngsi-ld:WeatherObserved:with space"}},
    Json{{"id", std::string(257, 'x')}}, Json{{"refDevice", "urn:ngsi-ld:Device:"}},
    Json{{"temperature", "23.4"}}, Json{{"temperature", nullptr}},
    Json{{"temperature", true}}, Json{{"relativeHumidity", 56}},
    Json{{"relativeHumidity", -0.01}}, Json{{"relativeHumidity", 1.01}},
    Json{{"relativeHumidity", "0.56"}}, Json{{"location", nullptr}},
    Json{{"location", {{"type", "Polygon"}, {"coordinates", {-8, 41}}}}},
    Json{{"location", {{"type", "Point"}, {"coordinates", {-181, 41}}}}},
    Json{{"location", {{"type", "Point"}, {"coordinates", {181, 41}}}}},
    Json{{"location", {{"type", "Point"}, {"coordinates", {-8, -91}}}}},
    Json{{"location", {{"type", "Point"}, {"coordinates", {-8, 91}}}}},
    Json{{"location", {{"type", "Point"}, {"coordinates", {-8}}}}},
    Json{{"location", {{"type", "Point"}, {"coordinates", {-8, 41, 0}}}}},
    Json{{"location", {{"type", "Point"}, {"coordinates", {"-8", 41}}}}},
    Json{{"location", {{"type", "Point"}, {"coordinates", Json::object()}}}},
    Json{{"location", {{"type", "Point"}}}},
    Json{{"location", {{"coordinates", {-8, 41}}}}}));

class InvalidTimestamp : public testing::TestWithParam<const char*> {};

TEST_P(InvalidTimestamp, RejectsInvalidDatesAndNonUtcFormats) {
    auto document = Json::parse(encode(reading()));
    document["payload"]["dateObserved"] = GetParam();
    expect_error(document.dump(), ErrorCode::InvalidPayload);
}

INSTANTIATE_TEST_SUITE_P(Timestamps, InvalidTimestamp, testing::Values(
    "2026-11-02", "2026-11-02T14:30:01+00:00", "2026-11-02T14:30:01",
    "2026-02-29T14:30:01Z", "1900-02-29T14:30:01Z", "0000-01-01T00:00:00Z",
    "2026-00-01T00:00:00Z", "2026-13-01T00:00:00Z", "2026-04-31T00:00:00Z",
    "2026-01-00T00:00:00Z", "2026-01-01T24:00:00Z", "2026-01-01T00:60:00Z",
    "2026-01-01T00:00:60Z", "2026-01-01T00:00:00.Z", "2026-01-01T00:00:00.aZ",
    "2026-01-01T00:00:00.1234567890Z"));

TEST(Protocol, AcceptsLeapDaysFractionalSecondsAndSequenceBoundaries) {
    auto original = reading();
    for (const auto* timestamp : {"2000-02-29T00:00:00Z", "2024-02-29T23:59:59.1Z",
                                  "2026-11-02T14:30:01.123456789Z"}) {
        original.payload.date_observed = timestamp;
        EXPECT_NO_THROW(decode(encode(original)));
    }
    original.identity.seq = 0;
    EXPECT_EQ(std::get<Reading>(decode(encode(original))).identity.seq, 0);
    original.identity.seq = std::numeric_limits<std::uint32_t>::max();
    EXPECT_EQ(std::get<Reading>(decode(encode(original))).identity.seq, original.identity.seq);
}

TEST(Protocol, AcceptsEquipmentViolationsForAlertEvaluation) {
    auto original = reading();
    for (double temperature : {-10.0, 0.0, 50.0, 70.0}) {
        for (double humidity : {0.0, 0.19, 0.2, 0.8, 0.81, 1.0}) {
            original.payload.temperature = temperature;
            original.payload.relative_humidity = humidity;
            EXPECT_NO_THROW(decode(encode(original)));
        }
    }
}

TEST(Protocol, EnforcesDatagramLimitIncludingWhitespace) {
    auto wire = encode(Ack{reading().identity});
    wire.resize(max_datagram_size, ' ');
    EXPECT_NO_THROW(decode(wire));
    wire.push_back(' ');
    expect_error(wire, ErrorCode::DatagramTooLarge);
}

TEST(Protocol, IgnoresUnknownFieldsWithinKnownVersion) {
    auto document = Json::parse(encode(reading()));
    document["extension"] = "future metadata";
    document["payload"]["description"] = "cell A";
    EXPECT_NO_THROW(decode(document.dump()));
}

TEST(Protocol, RejectsInvalidTypedValuesDuringEncoding) {
    auto original = reading();
    original.payload.temperature = std::numeric_limits<double>::quiet_NaN();
    EXPECT_THROW(encode(original), ProtocolError);
    original = reading();
    original.payload.relative_humidity = std::numeric_limits<double>::infinity();
    EXPECT_THROW(encode(original), ProtocolError);
    original = reading();
    original.qos = static_cast<Qos>(2);
    EXPECT_THROW(encode(original), ProtocolError);
    original = reading();
    original.identity.node_id.clear();
    EXPECT_THROW(encode(original), ProtocolError);
    EXPECT_THROW(encode(Reject{reading().identity, static_cast<RejectCode>(99), "bad"}), ProtocolError);
}

TEST(Protocol, ValidatesRejectionFields) {
    auto document = Json::parse(encode(Reject{reading().identity, RejectCode::InvalidPayload, "bad"}));
    for (const auto* name : {"code", "reason"}) {
        auto missing = document;
        missing.erase(name);
        expect_error(missing.dump(), ErrorCode::InvalidMessage);
    }
    document["code"] = "retry";
    expect_error(document.dump(), ErrorCode::InvalidMessage);
    document["code"] = "invalid_payload";
    for (const auto& reason : {std::string{}, std::string(257, 'x'), std::string("bad\nreason")}) {
        document["reason"] = reason;
        expect_error(document.dump(), ErrorCode::InvalidMessage);
    }
}

TEST(Protocol, RejectsDuplicateKeysAtEveryObjectLevel) {
    const auto wire = encode(reading());
    expect_error("{\"version\":1," + wire.substr(1), ErrorCode::InvalidJson);
    auto nested = wire;
    const auto payload = nested.find("\"payload\":{");
    ASSERT_NE(payload, std::string::npos);
    nested.insert(payload + std::string("\"payload\":{").size(), "\"temperature\":0,");
    expect_error(nested, ErrorCode::InvalidJson);
    expect_error("{\"unknown\":{\"key\":1,\"key\":2}}", ErrorCode::InvalidJson);
}

TEST(Protocol, AcceptsGeographicAndIdentifierBoundaries) {
    auto original = reading();
    original.identity.node_id = std::string(64, 'a');
    original.identity.session_id = std::string(64, 'b');
    original.payload.id = "urn:ngsi-ld:WeatherObserved:";
    original.payload.id.resize(256, 'a');
    original.payload.ref_device = "urn:ngsi-ld:Device:";
    original.payload.ref_device.resize(256, 'b');
    ASSERT_EQ(original.payload.id.size(), 256);
    ASSERT_EQ(original.payload.ref_device.size(), 256);
    for (double longitude : {-180.0, 180.0}) {
        for (double latitude : {-90.0, 90.0}) {
            original.payload.location = {longitude, latitude};
            EXPECT_NO_THROW(decode(encode(original)));
        }
    }
}

TEST(Protocol, ValidatesPayloadObjectTypesAndHugeNumbers) {
    auto document = Json::parse(encode(reading()));
    for (const auto& payload : {Json(nullptr), Json::array(), Json("reading"), Json(42)}) {
        document["payload"] = payload;
        expect_error(document.dump(), ErrorCode::InvalidPayload);
    }
    document = Json::parse(encode(reading()));
    document["payload"]["temperature"] = std::numeric_limits<double>::max();
    EXPECT_NO_THROW(decode(document.dump()));
}

TEST(Protocol, RejectsNonFiniteCoordinatesAndInvalidUtf8OnEncode) {
    auto original = reading();
    original.payload.location.longitude = std::numeric_limits<double>::infinity();
    EXPECT_THROW(encode(original), ProtocolError);
    original = reading();
    original.payload.location.latitude = std::numeric_limits<double>::quiet_NaN();
    EXPECT_THROW(encode(original), ProtocolError);
    EXPECT_THROW(encode(Reject{reading().identity, RejectCode::InvalidPayload, std::string(1, '\xff')}),
                 ProtocolError);
}

TEST(Protocol, EnforcesEncodedDatagramSizeAfterJsonEscaping) {
    auto original = reading();
    original.identity.node_id = std::string(64, 'a');
    original.identity.session_id = std::string(64, 'b');
    original.payload.id = "urn:ngsi-ld:WeatherObserved:";
    original.payload.id.resize(256, '\\');
    original.payload.ref_device = "urn:ngsi-ld:Device:";
    original.payload.ref_device.resize(256, '\\');
    try {
        encode(original);
        FAIL() << "Expected an oversized datagram";
    } catch (const ProtocolError& error) {
        EXPECT_EQ(error.code(), ErrorCode::DatagramTooLarge);
    }
}

}
#endif