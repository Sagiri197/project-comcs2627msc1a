#include "reading_store.h"

#include <sqlite3.h>

#include <limits>
#include <memory>
#include <utility>

namespace {

using Statement = std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)>;

[[noreturn]] void database_error(sqlite3* database, const char* operation) {
    throw PersistenceError(sqlite3_extended_errcode(database),
                           std::string(operation) + ": " + sqlite3_errmsg(database));
}

void execute(sqlite3* database, const char* sql) {
    if (sqlite3_exec(database, sql, nullptr, nullptr, nullptr) != SQLITE_OK) {
        database_error(database, "Execute SQL");
    }
}

Statement prepare(sqlite3* database, const char* sql) {
    sqlite3_stmt* statement = nullptr;
    const int result = sqlite3_prepare_v2(database, sql, -1, &statement, nullptr);
    Statement owned{statement, sqlite3_finalize};
    if (result != SQLITE_OK) database_error(database, "Prepare SQL");
    return owned;
}

void bind_text(sqlite3* database, sqlite3_stmt* statement, int index, const std::string& text) {
    if (sqlite3_bind_text(statement, index, text.data(), static_cast<int>(text.size()),
                          SQLITE_TRANSIENT) != SQLITE_OK) {
        database_error(database, "Bind text");
    }
}

void bind_identity(sqlite3* database, sqlite3_stmt* statement,
                   const common::protocol::Identity& identity) {
    bind_text(database, statement, 1, identity.node_id);
    bind_text(database, statement, 2, identity.session_id);
    if (sqlite3_bind_int64(statement, 3, identity.seq) != SQLITE_OK) {
        database_error(database, "Bind sequence number");
    }
}

int step(sqlite3* database, sqlite3_stmt* statement) {
    const int result = sqlite3_step(statement);
    if (result != SQLITE_ROW && result != SQLITE_DONE) database_error(database, "Step SQL");
    return result;
}

std::string stored_datagram(sqlite3_stmt* statement) {
    const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(statement, 0));
    if (text == nullptr) throw PersistenceError(SQLITE_CORRUPT, "Missing stored datagram");
    return {text, static_cast<std::size_t>(sqlite3_column_bytes(statement, 0))};
}

}

PersistenceError::PersistenceError(int code, const std::string& message)
    : std::runtime_error(message), m_code(code) {}

int PersistenceError::code() const noexcept { return m_code; }

ReadingStore::ReadingStore(const std::string& path) {
    if (path.empty() || path.find('\0') != std::string::npos) {
        throw std::invalid_argument("Database path must be nonempty and contain no NUL bytes");
    }
    const int result = sqlite3_open_v2(path.c_str(), &m_database,
                                       SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
    if (result != SQLITE_OK) {
        const std::string message = m_database ? sqlite3_errmsg(m_database) : "Cannot allocate database";
        sqlite3_close(m_database);
        m_database = nullptr;
        throw PersistenceError(result, message);
    }
    try {
        if (sqlite3_busy_timeout(m_database, 250) != SQLITE_OK) {
            database_error(m_database, "Set busy timeout");
        }
        execute(m_database, "PRAGMA journal_mode=WAL;");
        execute(m_database, "PRAGMA synchronous=FULL;");
        execute(m_database,
            "CREATE TABLE IF NOT EXISTS readings ("
            "node_id TEXT NOT NULL, session_id TEXT NOT NULL, "
            "seq INTEGER NOT NULL CHECK(seq BETWEEN 0 AND 4294967295), "
            "datagram TEXT NOT NULL, "
            "processed INTEGER NOT NULL DEFAULT 0 CHECK(processed IN (0, 1)), "
            "PRIMARY KEY(node_id, session_id, seq)) WITHOUT ROWID;");
    } catch (...) {
        sqlite3_close(m_database);
        m_database = nullptr;
        throw;
    }
}

ReadingStore::~ReadingStore() { sqlite3_close(m_database); }

ReadingStore::SaveResult ReadingStore::save(const common::protocol::Reading& reading) {
    const auto datagram = common::protocol::encode(reading);
    auto statement = prepare(m_database,
        "INSERT INTO readings(node_id, session_id, seq, datagram) VALUES(?1, ?2, ?3, ?4) "
        "ON CONFLICT(node_id, session_id, seq) DO NOTHING;");
    bind_identity(m_database, statement.get(), reading.identity);
    bind_text(m_database, statement.get(), 4, datagram);
    step(m_database, statement.get());
    if (sqlite3_changes(m_database) == 1) return SaveResult::Inserted;

    auto existing = prepare(m_database,
        "SELECT datagram FROM readings WHERE node_id=?1 AND session_id=?2 AND seq=?3;");
    bind_identity(m_database, existing.get(), reading.identity);
    if (step(m_database, existing.get()) != SQLITE_ROW) {
        throw PersistenceError(SQLITE_CORRUPT, "Duplicate reading is missing from database");
    }
    return stored_datagram(existing.get()) == datagram ? SaveResult::Duplicate : SaveResult::IdentityConflict;
}

std::optional<ReadingStore::StoredReading> ReadingStore::find(
    const common::protocol::Identity& identity) const {
    auto statement = prepare(m_database,
        "SELECT datagram, processed FROM readings WHERE node_id=?1 AND session_id=?2 AND seq=?3;");
    bind_identity(m_database, statement.get(), identity);
    if (step(m_database, statement.get()) == SQLITE_DONE) return std::nullopt;
    return StoredReading{std::get<common::protocol::Reading>(
                             common::protocol::decode(stored_datagram(statement.get()))),
                         sqlite3_column_int(statement.get(), 1) != 0};
}

std::vector<common::protocol::Reading> ReadingStore::pending(std::size_t limit) const {
    if (limit > static_cast<std::size_t>(std::numeric_limits<sqlite3_int64>::max())) {
        throw std::invalid_argument("Pending-reading limit is too large");
    }
    auto statement = prepare(m_database,
        "SELECT datagram FROM readings WHERE processed=0 "
        "ORDER BY node_id, session_id, seq LIMIT ?1;");
    if (sqlite3_bind_int64(statement.get(), 1, static_cast<sqlite3_int64>(limit)) != SQLITE_OK) {
        database_error(m_database, "Bind pending-reading limit");
    }
    std::vector<common::protocol::Reading> readings;
    while (step(m_database, statement.get()) == SQLITE_ROW) {
        readings.push_back(std::get<common::protocol::Reading>(
            common::protocol::decode(stored_datagram(statement.get()))));
    }
    return readings;
}

bool ReadingStore::mark_processed(const common::protocol::Identity& identity) {
    auto statement = prepare(m_database,
        "UPDATE readings SET processed=1 WHERE node_id=?1 AND session_id=?2 AND seq=?3 AND processed=0;");
    bind_identity(m_database, statement.get(), identity);
    step(m_database, statement.get());
    return sqlite3_changes(m_database) == 1;
}