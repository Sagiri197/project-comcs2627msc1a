#include "alert_server.h"
#include "fake_udp_client.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <sqlite3.h>

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace {

using namespace common::protocol;

Identity identity() { return {"esp32-a", "boot-1", 42}; }

Reading reading(Qos qos = Qos::AtLeastOnce) {
    return {identity(), qos,
            {"urn:ngsi-ld:WeatherObserved:esp32-a:boot-1:42", "2026-11-02T14:30:01Z",
             {-8.608, 41.178}, "urn:ngsi-ld:Device:esp32-a", 23.4, 0.56}};
}

std::vector<Message> messages() {
    auto unsynchronized = reading();
    unsynchronized.time_synchronized = false;
    return {reading(), reading(Qos::BestEffort), unsynchronized, Ack{identity()},
            Reject{identity(), RejectCode::InvalidMessage, "Invalid message"},
            Reject{identity(), RejectCode::UnsupportedVersion, "Unsupported version"},
            Reject{identity(), RejectCode::InvalidPayload, "Invalid observation"},
            Reject{identity(), RejectCode::IdentityConflict, "Identity already used"}};
}

class AlertServerTest : public testing::Test {
protected:
    ReadingStore store{":memory:"};
    AlertLogic logic{store};
    AlertServer server{store, logic, {"127.0.0.1", 0}};
    std::unique_ptr<FakeUdpClient> peer;
    UdpEndpoint peer_endpoint;
    FakeUdpClient::Endpoint server_endpoint;

    void SetUp() override {
        peer = std::make_unique<FakeUdpClient>();
        auto ep = peer->local_endpoint();
        peer_endpoint = {ep.address, static_cast<std::uint16_t>(ep.port)};

        server.send(Ack{identity()}, peer_endpoint);
        const auto bootstrap = peer->receive();
        ASSERT_FALSE(bootstrap.empty());
        
        auto sender = peer->last_sender();
        ASSERT_TRUE(sender);
        server_endpoint = *sender;
    }
};

class AlertServerMessageTest : public AlertServerTest,
                               public testing::WithParamInterface<Message> {};

TEST_P(AlertServerMessageTest, SendsEncodedProtocolDatagrams) {
    server.send(GetParam(), peer_endpoint);
    EXPECT_EQ(peer->receive(), encode(GetParam()));
}

TEST_P(AlertServerMessageTest, ReceivesTypedMessagesWithSenderEndpoint) {
    peer->send(encode(GetParam()), server_endpoint);
    const auto received = server.receive();
    EXPECT_EQ(received.message.index(), GetParam().index());
    EXPECT_EQ(encode(received.message), encode(GetParam()));
    EXPECT_EQ(received.sender.address, peer_endpoint.address);
    EXPECT_EQ(received.sender.port, peer_endpoint.port);
    if (const auto* original = std::get_if<Reading>(&GetParam())) {
        const auto stored = store.find(original->identity);
        ASSERT_TRUE(stored);
        EXPECT_EQ(stored->identity.node_id, original->identity.node_id);
        EXPECT_EQ(stored->identity.session_id, original->identity.session_id);
        EXPECT_EQ(stored->identity.seq, original->identity.seq);
        if (original->qos == Qos::AtLeastOnce) {
            EXPECT_EQ(encode(decode(peer->receive())), encode(Ack{original->identity}));
        }
    } else {
        EXPECT_FALSE(store.find(identity()));
    }
    peer->expect_no_reply();
}

INSTANTIATE_TEST_SUITE_P(MessageKinds, AlertServerMessageTest,
                        testing::ValuesIn(messages()));

TEST_F(AlertServerTest, RepliesToTheReceivedEndpoint) {
    peer->send(encode(reading()), server_endpoint);
    const auto received = server.receive();
    EXPECT_EQ(received.sender.port, peer_endpoint.port);
    const auto ack = std::get<Ack>(decode(peer->receive()));
    EXPECT_EQ(ack.identity.node_id, identity().node_id);
    EXPECT_EQ(ack.identity.session_id, identity().session_id);
    EXPECT_EQ(ack.identity.seq, identity().seq);
    peer->expect_no_reply();
}

TEST_F(AlertServerTest, PropagatesParseErrorsAndContinuesReceiving) {
    peer->send("not json", server_endpoint);
    try {
        server.receive();
        FAIL() << "Expected a protocol error";
    } catch (const ProtocolError& error) {
        EXPECT_EQ(error.code(), ErrorCode::InvalidJson);
    }
    peer->expect_no_reply();
    peer->send(encode(reading()), server_endpoint);
    EXPECT_EQ(encode(server.receive().message), encode(reading()));
}

TEST_F(AlertServerTest, PropagatesSchemaErrors) {
    peer->send("{\"version\":1}", server_endpoint);
    try {
        server.receive();
        FAIL() << "Expected a protocol error";
    } catch (const ProtocolError& error) {
        EXPECT_EQ(error.code(), ErrorCode::InvalidMessage);
    }
    peer->expect_no_reply();
}

TEST_F(AlertServerTest, RejectsOversizedDatagramsWithoutTruncation) {
    auto wire = encode(reading());
    wire.resize(max_datagram_size + 1, ' ');
    peer->send(wire, server_endpoint);
    try {
        server.receive();
        FAIL() << "Expected a protocol error";
    } catch (const ProtocolError& error) {
        EXPECT_EQ(error.code(), ErrorCode::DatagramTooLarge);
    }
    peer->send(encode(Ack{identity()}), server_endpoint);
    EXPECT_TRUE(std::holds_alternative<Ack>(server.receive().message));
}

TEST_F(AlertServerTest, AcceptsExactlyTheProtocolSizeLimit) {
    auto wire = encode(reading());
    wire.resize(max_datagram_size, ' ');
    peer->send(wire, server_endpoint);
    EXPECT_EQ(encode(server.receive().message), encode(reading()));
}

TEST_F(AlertServerTest, RejectsEmptyDatagrams) {
    peer->send("", server_endpoint);
    EXPECT_THROW(server.receive(), ProtocolError);
}

TEST_F(AlertServerTest, ValidatesMessagesBeforeSending) {
    auto invalid = reading();
    invalid.payload.relative_humidity = 56;
    EXPECT_THROW(server.send(invalid, peer_endpoint), ProtocolError);
    peer->expect_no_reply();
    server.send(reading(), peer_endpoint);
    EXPECT_EQ(peer->receive(), encode(reading()));
}

TEST_F(AlertServerTest, PreservesSeparateClientEndpoints) {
    const auto second_client = std::make_unique<FakeUdpClient>();
    auto second_reading = reading();
    second_reading.identity.node_id = "esp32-b";
    second_reading.payload.ref_device = "urn:ngsi-ld:Device:esp32-b";
    second_reading.payload.id = "urn:ngsi-ld:WeatherObserved:esp32-b:boot-1:42";

    peer->send(encode(reading()), server_endpoint);
    second_client->send(encode(second_reading), server_endpoint);
    const auto first = server.receive();
    const auto second = server.receive();
    ASSERT_TRUE(std::holds_alternative<Reading>(first.message));
    ASSERT_TRUE(std::holds_alternative<Reading>(second.message));
    EXPECT_NE(first.sender.port, second.sender.port);
    const auto& from_peer = first.sender.port == peer_endpoint.port ? first : second;
    const auto& from_second = first.sender.port == peer_endpoint.port ? second : first;
    EXPECT_EQ(from_peer.sender.port, peer_endpoint.port);
    EXPECT_EQ(std::get<Reading>(from_peer.message).identity.node_id, "esp32-a");
    EXPECT_EQ(std::get<Reading>(from_second.message).identity.node_id, "esp32-b");

    EXPECT_EQ(std::get<Ack>(decode(peer->receive())).identity.node_id, "esp32-a");
    const auto second_endpoint = FakeUdpClient::Endpoint{second_client->local_endpoint().address, second_client->local_endpoint().port};
    EXPECT_EQ(std::get<Ack>(decode(second_client->receive())).identity.node_id, "esp32-b");
}

TEST(AlertServer, RejectsInvalidBindAddress) {
    ReadingStore store(":memory:");
    AlertLogic logic(store);
    EXPECT_THROW((AlertServer{store, logic, {"invalid-address", 0}}), std::invalid_argument);
}

TEST_F(AlertServerTest, PropagatesInvalidDestinationErrors) {
    EXPECT_THROW(server.send(Ack{identity()}, {"invalid-address", 12345}), std::invalid_argument);
    peer->expect_no_reply();
}

TEST_F(AlertServerTest, ReturnsComparisonForAlertPublication) {
    // First reading from client A
    peer->send(encode(reading()), server_endpoint);
    const auto first = server.receive();
    EXPECT_FALSE(first.comparison);
    peer->receive();

    // Second reading from client B (different client)
    auto other_reading = reading();
    other_reading.identity.node_id = "esp32-b";  // Different client
    other_reading.identity.seq = 43;
    other_reading.payload.id = "urn:ngsi-ld:WeatherObserved:esp32-b:boot-1:43";
    other_reading.payload.temperature += 3.0;  // +3°C diff to trigger threshold
    peer->send(encode(other_reading), server_endpoint);
    const auto second = server.receive();

    ASSERT_TRUE(second.comparison);
    EXPECT_TRUE(second.comparison->exceeds_temp_threshold);
    EXPECT_EQ(std::get<Reading>(second.message).identity.seq, 43);
    EXPECT_TRUE(std::holds_alternative<Ack>(decode(peer->receive())));
}

TEST_F(AlertServerTest, PersistsReadingBeforeAcknowledgement) {
    peer->send(encode(reading()), server_endpoint);
    const auto received = server.receive();
    const auto& original = std::get<Reading>(received.message);
    const auto ack = std::get<Ack>(decode(peer->receive()));
    ASSERT_TRUE(store.find(ack.identity));
}

TEST_F(AlertServerTest, ReacknowledgesRetriesWithoutDuplicatingReadings) {
    peer->send(encode(reading()), server_endpoint);
    server.receive();
    EXPECT_TRUE(std::holds_alternative<Ack>(decode(peer->receive())));
    peer->send(encode(reading()), server_endpoint);
    server.receive();
    EXPECT_TRUE(std::holds_alternative<Ack>(decode(peer->receive())));
    EXPECT_EQ(store.save(reading()), ReadingStore::SaveResult::Duplicate);
    EXPECT_TRUE(store.find(reading().identity));
}

TEST_F(AlertServerTest, ReacknowledgesReadingCommittedBeforeLostAcknowledgement) {
    ASSERT_EQ(store.save(reading()), ReadingStore::SaveResult::Inserted);
    peer->send(encode(reading()), server_endpoint);
    server.receive();
    EXPECT_EQ(std::get<Ack>(decode(peer->receive())).identity.seq, reading().identity.seq);
    EXPECT_TRUE(store.find(reading().identity));
}

TEST_F(AlertServerTest, BestEffortPersistsWithoutAcknowledgement) {
    const auto original = reading(Qos::BestEffort);
    peer->send(encode(original), server_endpoint);
    server.receive();
    peer->expect_no_reply();
    ASSERT_TRUE(store.find(original.identity));
    peer->send(encode(original), server_endpoint);
    server.receive();
    EXPECT_EQ(store.save(original), ReadingStore::SaveResult::Duplicate);
    peer->expect_no_reply();
}

TEST_F(AlertServerTest, RejectsConflictsWithoutOverwritingAcceptedReading) {
    ASSERT_EQ(store.save(reading()), ReadingStore::SaveResult::Inserted);
    auto conflict = reading();
    conflict.payload.temperature += 1;
    peer->send(encode(conflict), server_endpoint);
    EXPECT_NO_THROW(server.receive());
    const auto ack = std::get<Ack>(decode(peer->receive()));
    EXPECT_EQ(ack.identity.seq, conflict.identity.seq);
    ASSERT_TRUE(store.find(conflict.identity));
}

TEST_F(AlertServerTest, InvalidReadingsAreNeitherStoredNorAcknowledged) {
    auto document = nlohmann::json::parse(encode(reading()));
    document["payload"]["relativeHumidity"] = 56;
    peer->send(document.dump(), server_endpoint);
    EXPECT_THROW(server.receive(), ProtocolError);
    EXPECT_FALSE(store.find(identity()));
    peer->expect_no_reply();
}

TEST_F(AlertServerTest, DatabaseFailureSendsNoAcknowledgementAndAllowsRetry) {
    char pattern[] = "/tmp/comics-server-XXXXXX";
    const auto* created = mkdtemp(pattern);
    ASSERT_NE(created, nullptr);
    const auto cleanup = [](char* directory) {
        std::error_code error;
        std::filesystem::remove_all(directory, error);
    };
    const std::unique_ptr<char, decltype(cleanup)> directory{pattern, cleanup};
    const auto path = (std::filesystem::path(created) / "readings.sqlite3").string();
    ReadingStore disk_store(path);
    AlertLogic logic(disk_store);
    AlertServer disk_server(disk_store, logic, {"127.0.0.1", 0});
    
    auto disk_client = std::make_unique<FakeUdpClient>();
    auto disk_ep = disk_client->local_endpoint();
    UdpEndpoint disk_peer_endpoint{disk_ep.address, static_cast<std::uint16_t>(disk_ep.port)};
    
    disk_server.send(Ack{identity()}, disk_peer_endpoint);
    const auto bootstrap = disk_client->receive();
    ASSERT_FALSE(bootstrap.empty());
    auto disk_server_endpoint = disk_client->last_sender();
    ASSERT_TRUE(disk_server_endpoint);
    
    sqlite3* raw = nullptr;
    ASSERT_EQ(sqlite3_open(path.c_str(), &raw), SQLITE_OK);
    const std::unique_ptr<sqlite3, decltype(&sqlite3_close)> blocker{raw, sqlite3_close};
    ASSERT_EQ(sqlite3_exec(raw, "BEGIN IMMEDIATE;", nullptr, nullptr, nullptr), SQLITE_OK);
    disk_client->send(encode(reading()), *disk_server_endpoint);
    EXPECT_THROW(disk_server.receive(), PersistenceError);
    disk_client->expect_no_reply();
    EXPECT_FALSE(disk_store.find(reading().identity));
    ASSERT_EQ(sqlite3_exec(raw, "ROLLBACK;", nullptr, nullptr, nullptr), SQLITE_OK);
    disk_client->send(encode(reading()), *disk_server_endpoint);
    EXPECT_NO_THROW(disk_server.receive());
    EXPECT_TRUE(std::holds_alternative<Ack>(decode(disk_client->receive())));
    ReadingStore observer(path);
    EXPECT_TRUE(observer.find(reading().identity));
}

static_assert(!std::is_copy_constructible_v<AlertServer>);
static_assert(!std::is_copy_assignable_v<AlertServer>);

}