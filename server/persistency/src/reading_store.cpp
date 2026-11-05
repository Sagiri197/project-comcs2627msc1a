#include "reading_store.h"

#include <sqlite3.h>

#include <memory>

namespace {

using Statement = std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)>;

[[noreturn]] void database_error(sqlite3* database, const char* operation) {
    throw PersistenceError(sqlite3_extended_errcode(database),
                           std::string(operation) + ": " + sqlite3_errmsg(database));
}

Statement prepare(sqlite3* database, const char* sql) {
    sqlite3_stmt* statement = nullptr;
    const int result = sqlite3_prepare_v2(database, sql, -1, &statement, nullptr);
    Statement owned{statement, sqlite3_finalize};
    if (result != SQLITE_OK) database_error(database, "Prepare SQL");
    return owned;
}

void bind_identity(sqlite3* database, sqlite3_stmt* statement,
                   const common::protocol::Identity& identity) {
    if (sqlite3_bind_text(statement, 1, identity.node_id.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_text(statement, 2, identity.session_id.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_int64(statement, 3, identity.seq) != SQLITE_OK) {
        database_error(database, "Bind reading identity");
    }
}

}

PersistenceError::PersistenceError(int code, const std::string& message)
    : std::runtime_error(message), m_code(code) {}

int PersistenceError::code() const noexcept { 
    return m_code; 
}

ReadingStore::ReadingStore(const std::string& path) {
    
    if (path.empty() || path.find('\0') != std::string::npos) {
        throw std::invalid_argument("Database path must be nonempty and contain no NUL bytes");
    }
    
    const int result = sqlite3_open(path.c_str(), &m_database);
    if (result != SQLITE_OK) {
        const std::string message = m_database ? sqlite3_errmsg(m_database) : "Cannot allocate database";
        sqlite3_close(m_database);
        m_database = nullptr;
        throw PersistenceError(result, message);
    }

    try {
        const char* setup =
            "PRAGMA journal_mode=WAL;"
            "PRAGMA synchronous=FULL;"
            "CREATE TABLE IF NOT EXISTS readings ("
            "node_id TEXT NOT NULL, session_id TEXT NOT NULL, "
            "seq INTEGER NOT NULL CHECK(seq BETWEEN 0 AND 4294967295), "
            "datagram TEXT NOT NULL, "
            "PRIMARY KEY(node_id, session_id, seq)) WITHOUT ROWID;";
    
        if (sqlite3_busy_timeout(m_database, 250) != SQLITE_OK ||
            sqlite3_exec(m_database, setup, nullptr, nullptr, nullptr) != SQLITE_OK) {
            database_error(m_database, "Initialize database");
        }

    } catch (...) {
        sqlite3_close(m_database);
        m_database = nullptr;
        throw;
    }
}

ReadingStore::~ReadingStore() { 
    sqlite3_close(m_database); 
}

ReadingStore::SaveResult ReadingStore::save(const common::protocol::Reading& reading) {
    
    const auto datagram = common::protocol::encode(reading);
    auto statement = prepare(m_database,
        "INSERT INTO readings(node_id, session_id, seq, datagram) VALUES(?1, ?2, ?3, ?4) "
        "ON CONFLICT(node_id, session_id, seq) DO NOTHING;");
    
    bind_identity(m_database, statement.get(), reading.identity);
    if (sqlite3_bind_text(statement.get(), 4, datagram.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_step(statement.get()) != SQLITE_DONE) {
        database_error(m_database, "Save reading");
    }
    
    if (sqlite3_changes(m_database) == 1) return SaveResult::Inserted;

    const auto existing = find(reading.identity);
    if (!existing)
        throw PersistenceError(SQLITE_CORRUPT, "Duplicate reading is missing from database");

    return common::protocol::encode(*existing) == datagram
        ? SaveResult::Duplicate : SaveResult::IdentityConflict;
}

std::optional<common::protocol::Reading> ReadingStore::find(

    const common::protocol::Identity& identity) const {
    auto statement = prepare(m_database,
        "SELECT datagram FROM readings WHERE node_id=?1 AND session_id=?2 AND seq=?3;");
    
    bind_identity(m_database, statement.get(), identity);
    
    const int result = sqlite3_step(statement.get());
    if (result == SQLITE_DONE) return std::nullopt;
    if (result != SQLITE_ROW) database_error(m_database, "Find reading");
    
    const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(statement.get(), 0));
    if (text == nullptr) throw PersistenceError(SQLITE_CORRUPT, "Missing stored datagram");
    
    const std::string_view datagram{text, static_cast<std::size_t>(sqlite3_column_bytes(statement.get(), 0))};
    return std::get<common::protocol::Reading>(common::protocol::decode(datagram));
}