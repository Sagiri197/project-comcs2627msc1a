#include "alert_server.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <sqlite3.h>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <cerrno>
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
    AlertServer server{store, {"127.0.0.1", 0}};
    int peer = -1;
    UdpEndpoint peer_endpoint;
    UdpEndpoint server_endpoint;
    sockaddr_in server_address{};

    void SetUp() override {
        peer = ::socket(AF_INET, SOCK_DGRAM, 0);
        ASSERT_NE(peer, -1);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        ASSERT_EQ(bind(peer, reinterpret_cast<const sockaddr*>(&address), sizeof(address)), 0);
        socklen_t address_size = sizeof(address);
        ASSERT_EQ(getsockname(peer, reinterpret_cast<sockaddr*>(&address), &address_size), 0);
        peer_endpoint = {"127.0.0.1", ntohs(address.sin_port)};

        timeval timeout{1, 0};
        ASSERT_EQ(setsockopt(peer, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)), 0);

        server.send(Ack{identity()}, peer_endpoint);
        const auto bootstrap = receive_wire();
        ASSERT_FALSE(bootstrap.empty());
        server_endpoint = {"127.0.0.1", ntohs(server_address.sin_port)};
    }

    void TearDown() override {
        if (peer != -1) close(peer);
    }

    std::string receive_wire() {
        std::string wire(65535, '\0');
        socklen_t address_size = sizeof(server_address);
        const auto received = recvfrom(peer, wire.data(), wire.size(), 0,
                                       reinterpret_cast<sockaddr*>(&server_address), &address_size);
        EXPECT_GE(received, 0);
        if (received < 0) return {};
        wire.resize(static_cast<std::size_t>(received));
        return wire;
    }

    void send_wire(const std::string& wire) {
        ASSERT_EQ(sendto(peer, wire.data(), wire.size(), 0,
                         reinterpret_cast<const sockaddr*>(&server_address), sizeof(server_address)),
                  static_cast<ssize_t>(wire.size()));
    }

    void expect_no_reply() {
        char buffer{};
        errno = 0;
        EXPECT_EQ(::recv(peer, &buffer, sizeof(buffer), MSG_DONTWAIT), -1);
        EXPECT_TRUE(errno == EAGAIN || errno == EWOULDBLOCK);
    }
};

class AlertServerMessageTest : public AlertServerTest,
                               public testing::WithParamInterface<Message> {};

TEST_P(AlertServerMessageTest, SendsEncodedProtocolDatagrams) {
    server.send(GetParam(), peer_endpoint);
    EXPECT_EQ(receive_wire(), encode(GetParam()));
}

TEST_P(AlertServerMessageTest, ReceivesTypedMessagesWithSenderEndpoint) {
    send_wire(encode(GetParam()));
    const auto received = server.receive();
    EXPECT_EQ(received.message.index(), GetParam().index());
    EXPECT_EQ(encode(received.message), encode(GetParam()));
    EXPECT_EQ(received.sender.address, peer_endpoint.address);
    EXPECT_EQ(received.sender.port, peer_endpoint.port);
    if (const auto* original = std::get_if<Reading>(&GetParam())) {
        ASSERT_TRUE(store.find(original->identity));
        EXPECT_EQ(encode(*store.find(original->identity)), encode(*original));
        if (original->qos == Qos::AtLeastOnce) {
            EXPECT_EQ(encode(decode(receive_wire())), encode(Ack{original->identity}));
        }
    } else {
        EXPECT_FALSE(store.find(identity()));
    }
    expect_no_reply();
}

INSTANTIATE_TEST_SUITE_P(MessageKinds, AlertServerMessageTest,
                        testing::ValuesIn(messages()));

TEST_F(AlertServerTest, RepliesToTheReceivedEndpoint) {
    send_wire(encode(reading()));
    const auto received = server.receive();
    EXPECT_EQ(received.sender.port, peer_endpoint.port);
    const auto ack = std::get<Ack>(decode(receive_wire()));
    EXPECT_EQ(ack.identity.node_id, identity().node_id);
    EXPECT_EQ(ack.identity.session_id, identity().session_id);
    EXPECT_EQ(ack.identity.seq, identity().seq);
    expect_no_reply();
}

TEST_F(AlertServerTest, PropagatesParseErrorsAndContinuesReceiving) {
    send_wire("not json");
    try {
        server.receive();
        FAIL() << "Expected a protocol error";
    } catch (const ProtocolError& error) {
        EXPECT_EQ(error.code(), ErrorCode::InvalidJson);
    }
    expect_no_reply();
    send_wire(encode(reading()));
    EXPECT_EQ(encode(server.receive().message), encode(reading()));
}

TEST_F(AlertServerTest, PropagatesSchemaErrors) {
    send_wire("{\"version\":1}");
    try {
        server.receive();
        FAIL() << "Expected a protocol error";
    } catch (const ProtocolError& error) {
        EXPECT_EQ(error.code(), ErrorCode::InvalidMessage);
    }
    expect_no_reply();
}

TEST_F(AlertServerTest, RejectsOversizedDatagramsWithoutTruncation) {
    auto wire = encode(reading());
    wire.resize(max_datagram_size + 1, ' ');
    send_wire(wire);
    try {
        server.receive();
        FAIL() << "Expected a protocol error";
    } catch (const ProtocolError& error) {
        EXPECT_EQ(error.code(), ErrorCode::DatagramTooLarge);
    }
    send_wire(encode(Ack{identity()}));
    EXPECT_TRUE(std::holds_alternative<Ack>(server.receive().message));
}

TEST_F(AlertServerTest, AcceptsExactlyTheProtocolSizeLimit) {
    auto wire = encode(reading());
    wire.resize(max_datagram_size, ' ');
    send_wire(wire);
    EXPECT_EQ(encode(server.receive().message), encode(reading()));
}

TEST_F(AlertServerTest, RejectsEmptyDatagrams) {
    send_wire("");
    EXPECT_THROW(server.receive(), ProtocolError);
}

TEST_F(AlertServerTest, ValidatesMessagesBeforeSending) {
    auto invalid = reading();
    invalid.payload.relative_humidity = 56;
    EXPECT_THROW(server.send(invalid, peer_endpoint), ProtocolError);
    expect_no_reply();
    server.send(reading(), peer_endpoint);
    EXPECT_EQ(receive_wire(), encode(reading()));
}

TEST_F(AlertServerTest, PreservesSeparateClientEndpoints) {
    UdpSocket second_client{{"127.0.0.1", 0}};
    auto second_reading = reading();
    second_reading.identity.node_id = "esp32-b";
    second_reading.payload.ref_device = "urn:ngsi-ld:Device:esp32-b";
    second_reading.payload.id = "urn:ngsi-ld:WeatherObserved:esp32-b:boot-1:42";

    send_wire(encode(reading()));
    second_client.send(encode(second_reading), server_endpoint);
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

    EXPECT_EQ(std::get<Ack>(decode(receive_wire())).identity.node_id, "esp32-a");
    EXPECT_EQ(std::get<Ack>(decode(second_client.recv().data)).identity.node_id, "esp32-b");
}

TEST(AlertServer, RejectsInvalidBindAddress) {
    ReadingStore store(":memory:");
    EXPECT_THROW((AlertServer{store, {"invalid-address", 0}}), std::invalid_argument);
}

TEST_F(AlertServerTest, PropagatesInvalidDestinationErrors) {
    EXPECT_THROW(server.send(Ack{identity()}, {"invalid-address", 12345}), std::invalid_argument);
    expect_no_reply();
}

TEST_F(AlertServerTest, PersistsReadingBeforeAcknowledgement) {
    send_wire(encode(reading()));
    const auto received = server.receive();
    const auto& original = std::get<Reading>(received.message);
    const auto ack = std::get<Ack>(decode(receive_wire()));
    ASSERT_TRUE(store.find(ack.identity));
}

TEST_F(AlertServerTest, ReacknowledgesRetriesWithoutDuplicatingReadings) {
    send_wire(encode(reading()));
    server.receive();
    EXPECT_TRUE(std::holds_alternative<Ack>(decode(receive_wire())));
    send_wire(encode(reading()));
    server.receive();
    EXPECT_TRUE(std::holds_alternative<Ack>(decode(receive_wire())));
    EXPECT_EQ(store.save(reading()), ReadingStore::SaveResult::Duplicate);
    EXPECT_TRUE(store.find(reading().identity));
}

TEST_F(AlertServerTest, ReacknowledgesReadingCommittedBeforeLostAcknowledgement) {
    ASSERT_EQ(store.save(reading()), ReadingStore::SaveResult::Inserted);
    send_wire(encode(reading()));
    server.receive();
    EXPECT_EQ(std::get<Ack>(decode(receive_wire())).identity.seq, reading().identity.seq);
    EXPECT_TRUE(store.find(reading().identity));
}

TEST_F(AlertServerTest, BestEffortPersistsWithoutAcknowledgement) {
    const auto original = reading(Qos::BestEffort);
    send_wire(encode(original));
    server.receive();
    expect_no_reply();
    ASSERT_TRUE(store.find(original.identity));
    send_wire(encode(original));
    server.receive();
    EXPECT_EQ(store.save(original), ReadingStore::SaveResult::Duplicate);
    expect_no_reply();
}

TEST_F(AlertServerTest, RejectsConflictsWithoutOverwritingAcceptedReading) {
    ASSERT_EQ(store.save(reading()), ReadingStore::SaveResult::Inserted);
    auto conflict = reading();
    conflict.payload.temperature += 1;
    send_wire(encode(conflict));
    EXPECT_NO_THROW(server.receive());
    const auto ack = std::get<Ack>(decode(receive_wire()));
    EXPECT_EQ(ack.identity.seq, conflict.identity.seq);
    ASSERT_TRUE(store.find(conflict.identity));
}

TEST_F(AlertServerTest, InvalidReadingsAreNeitherStoredNorAcknowledged) {
    auto document = nlohmann::json::parse(encode(reading()));
    document["payload"]["relativeHumidity"] = 56;
    send_wire(document.dump());
    EXPECT_THROW(server.receive(), ProtocolError);
    EXPECT_FALSE(store.find(identity()));
    expect_no_reply();
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
    AlertServer disk_server(disk_store, {"127.0.0.1", 0});
    disk_server.send(Ack{identity()}, peer_endpoint);
    ASSERT_FALSE(receive_wire().empty());
    sqlite3* raw = nullptr;
    ASSERT_EQ(sqlite3_open(path.c_str(), &raw), SQLITE_OK);
    const std::unique_ptr<sqlite3, decltype(&sqlite3_close)> blocker{raw, sqlite3_close};
    ASSERT_EQ(sqlite3_exec(raw, "BEGIN IMMEDIATE;", nullptr, nullptr, nullptr), SQLITE_OK);
    send_wire(encode(reading()));
    EXPECT_THROW(disk_server.receive(), PersistenceError);
    expect_no_reply();
    EXPECT_FALSE(disk_store.find(reading().identity));
    ASSERT_EQ(sqlite3_exec(raw, "ROLLBACK;", nullptr, nullptr, nullptr), SQLITE_OK);
    send_wire(encode(reading()));
    EXPECT_NO_THROW(disk_server.receive());
    EXPECT_TRUE(std::holds_alternative<Ack>(decode(receive_wire())));
    ReadingStore observer(path);
    EXPECT_TRUE(observer.find(reading().identity));
}

static_assert(!std::is_copy_constructible_v<AlertServer>);
static_assert(!std::is_copy_assignable_v<AlertServer>);

}