#include "reading_store.h"

#include <gtest/gtest.h>
#include <sqlite3.h>

#include <sys/wait.h>
#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <limits>
#include <memory>
#include <type_traits>

namespace {

using namespace common::protocol;
using SaveResult = ReadingStore::SaveResult;

Reading reading() {
    return {{"esp32-a", "boot-1", 42}, Qos::AtLeastOnce,
            {"urn:ngsi-ld:WeatherObserved:esp32-a:boot-1:42", "2026-11-02T14:30:01Z",
             {-8.608, 41.178}, "urn:ngsi-ld:Device:esp32-a", 23.4, 0.56}};
}

class ReadingStoreTest : public testing::Test {
protected:
    std::filesystem::path directory;
    std::string path;

    void SetUp() override {
        char pattern[] = "/tmp/comics-persistency-XXXXXX";
        const auto* created = mkdtemp(pattern);
        ASSERT_NE(created, nullptr);
        directory = created;
        path = (directory / "readings.sqlite3").string();
    }

    void TearDown() override {
        if (!directory.empty()) {
            std::error_code error;
            std::filesystem::remove_all(directory, error);
        }
    }
};

TEST_F(ReadingStoreTest, StoresAndRetrievesCompleteReading) {
    ReadingStore store(path);
    auto original = reading();
    original.time_synchronized = false;
    EXPECT_EQ(store.save(original), SaveResult::Inserted);
    const auto stored = store.find(original.identity);
    ASSERT_TRUE(stored);
    EXPECT_EQ(encode(*stored), encode(original));
}

TEST_F(ReadingStoreTest, ReturnsNoReadingForUnknownIdentity) {
    ReadingStore store(path);
    EXPECT_FALSE(store.find(reading().identity));
}

TEST_F(ReadingStoreTest, IdenticalRetriesDoNotInsertAnotherReading) {
    ReadingStore store(path);
    EXPECT_EQ(store.save(reading()), SaveResult::Inserted);
    EXPECT_EQ(store.save(reading()), SaveResult::Duplicate);
    EXPECT_EQ(store.save(reading()), SaveResult::Duplicate);
    ASSERT_TRUE(store.find(reading().identity));
    EXPECT_EQ(encode(*store.find(reading().identity)), encode(reading()));
}

TEST_F(ReadingStoreTest, ConflictingIdentityDoesNotOverwriteOriginal) {
    ReadingStore store(path);
    const auto original = reading();
    ASSERT_EQ(store.save(original), SaveResult::Inserted);
    auto conflicting = original;
    conflicting.payload.temperature += 1;
    EXPECT_EQ(store.save(conflicting), SaveResult::IdentityConflict);
    conflicting = original;
    conflicting.time_synchronized = false;
    EXPECT_EQ(store.save(conflicting), SaveResult::IdentityConflict);
    conflicting = original;
    conflicting.qos = Qos::BestEffort;
    EXPECT_EQ(store.save(conflicting), SaveResult::IdentityConflict);
    EXPECT_EQ(encode(*store.find(original.identity)), encode(original));
}

TEST_F(ReadingStoreTest, NodeSessionAndSequenceAllParticipateInIdentity) {
    ReadingStore store(path);
    auto original = reading();
    EXPECT_EQ(store.save(original), SaveResult::Inserted);
    auto other = original;
    other.identity.node_id = "esp32-b";
    EXPECT_EQ(store.save(other), SaveResult::Inserted);
    EXPECT_TRUE(store.find(other.identity));
    other = original;
    other.identity.session_id = "boot-2";
    EXPECT_EQ(store.save(other), SaveResult::Inserted);
    EXPECT_TRUE(store.find(other.identity));
    other = original;
    ++other.identity.seq;
    EXPECT_EQ(store.save(other), SaveResult::Inserted);
    EXPECT_TRUE(store.find(other.identity));
    EXPECT_TRUE(store.find(original.identity));
}

TEST_F(ReadingStoreTest, SupportsSequenceNumberBoundaries) {
    ReadingStore store(path);
    auto original = reading();
    for (auto sequence : {0U, std::numeric_limits<std::uint32_t>::max()}) {
        original.identity.seq = sequence;
        ASSERT_EQ(store.save(original), SaveResult::Inserted);
        ASSERT_TRUE(store.find(original.identity));
        EXPECT_EQ(store.find(original.identity)->identity.seq, sequence);
    }
}

TEST_F(ReadingStoreTest, RecoversReadingsAndDeduplicationAfterReopen) {
    auto other = reading();
    ++other.identity.seq;
    {
        ReadingStore store(path);
        ASSERT_EQ(store.save(reading()), SaveResult::Inserted);
        ASSERT_EQ(store.save(other), SaveResult::Inserted);
    }
    ReadingStore reopened(path);
    EXPECT_EQ(reopened.save(reading()), SaveResult::Duplicate);
    EXPECT_EQ(reopened.save(other), SaveResult::Duplicate);
    ASSERT_TRUE(reopened.find(other.identity));
    EXPECT_EQ(encode(*reopened.find(other.identity)), encode(other));
    ASSERT_TRUE(reopened.find(reading().identity));
    EXPECT_EQ(encode(*reopened.find(reading().identity)), encode(reading()));
}

TEST_F(ReadingStoreTest, RecoversCommittedReadingAfterAbruptProcessExit) {
    const auto child = fork();
    ASSERT_NE(child, -1);
    if (child == 0) {
        try {
            ReadingStore store(path);
            if (store.save(reading()) != SaveResult::Inserted) _exit(2);
            _exit(0);
        } catch (...) {
            _exit(1);
        }
    }
    int status = 0;
    ASSERT_EQ(waitpid(child, &status, 0), child);
    ASSERT_TRUE(WIFEXITED(status));
    ASSERT_EQ(WEXITSTATUS(status), 0);
    ReadingStore recovered(path);
    EXPECT_EQ(recovered.save(reading()), SaveResult::Duplicate);
    ASSERT_TRUE(recovered.find(reading().identity));
    EXPECT_EQ(encode(*recovered.find(reading().identity)), encode(reading()));
}

TEST_F(ReadingStoreTest, IndependentConnectionsObserveCommittedReadings) {
    ReadingStore first(path);
    ReadingStore second(path);
    ASSERT_EQ(first.save(reading()), SaveResult::Inserted);
    ASSERT_TRUE(second.find(reading().identity));
    EXPECT_EQ(second.save(reading()), SaveResult::Duplicate);
    EXPECT_EQ(encode(*second.find(reading().identity)), encode(*first.find(reading().identity)));
}

TEST_F(ReadingStoreTest, RejectsInvalidProtocolWithoutWriting) {
    ReadingStore store(path);
    auto invalid = reading();
    invalid.payload.relative_humidity = 56;
    EXPECT_THROW(store.save(invalid), ProtocolError);
    EXPECT_FALSE(store.find(invalid.identity));
}

TEST_F(ReadingStoreTest, UsesBoundParametersForUntrustedText) {
    ReadingStore store(path);
    auto original = reading();
    original.payload.id = "urn:ngsi-ld:WeatherObserved:';DROP_TABLE_readings;--";
    ASSERT_EQ(store.save(original), SaveResult::Inserted);
    EXPECT_EQ(encode(*store.find(original.identity)), encode(original));
    auto next = reading();
    ++next.identity.seq;
    EXPECT_EQ(store.save(next), SaveResult::Inserted);
}

TEST_F(ReadingStoreTest, LockedDatabaseDoesNotAcceptReading) {
    ReadingStore store(path);
    sqlite3* raw = nullptr;
    ASSERT_EQ(sqlite3_open(path.c_str(), &raw), SQLITE_OK);
    const std::unique_ptr<sqlite3, decltype(&sqlite3_close)> blocker{raw, sqlite3_close};
    ASSERT_EQ(sqlite3_exec(raw, "BEGIN IMMEDIATE;", nullptr, nullptr, nullptr), SQLITE_OK);
    try {
        store.save(reading());
        FAIL() << "Expected a database lock error";
    } catch (const PersistenceError& error) {
        EXPECT_EQ(error.code() & 0xff, SQLITE_BUSY);
        EXPECT_FALSE(std::string(error.what()).empty());
    }
    EXPECT_FALSE(store.find(reading().identity));
    ASSERT_EQ(sqlite3_exec(raw, "ROLLBACK;", nullptr, nullptr, nullptr), SQLITE_OK);
    EXPECT_EQ(store.save(reading()), SaveResult::Inserted);
}

TEST_F(ReadingStoreTest, ReportsUnusableDatabasePaths) {
    EXPECT_THROW(ReadingStore(directory.string()), PersistenceError);
    EXPECT_THROW(ReadingStore((directory / "missing" / "readings.sqlite3").string()), PersistenceError);
    EXPECT_THROW(ReadingStore(""), std::invalid_argument);
    EXPECT_THROW(ReadingStore(std::string("database\0hidden", 15)), std::invalid_argument);
}

TEST_F(ReadingStoreTest, OpensDatabasesWithTheOldProcessedColumn) {
    sqlite3* raw = nullptr;
    ASSERT_EQ(sqlite3_open(path.c_str(), &raw), SQLITE_OK);
    const std::unique_ptr<sqlite3, decltype(&sqlite3_close)> database{raw, sqlite3_close};
    ASSERT_EQ(sqlite3_exec(raw,
        "CREATE TABLE readings(node_id TEXT, session_id TEXT, seq INTEGER, datagram TEXT, "
        "processed INTEGER NOT NULL DEFAULT 0, PRIMARY KEY(node_id, session_id, seq));",
        nullptr, nullptr, nullptr), SQLITE_OK);
    ReadingStore store(path);
    ASSERT_EQ(store.save(reading()), SaveResult::Inserted);
    ASSERT_TRUE(store.find(reading().identity));
    EXPECT_EQ(encode(*store.find(reading().identity)), encode(reading()));
    EXPECT_EQ(store.save(reading()), SaveResult::Duplicate);
}

static_assert(!std::is_copy_constructible_v<ReadingStore>);
static_assert(!std::is_copy_assignable_v<ReadingStore>);

}