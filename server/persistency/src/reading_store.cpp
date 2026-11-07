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
            "longitude REAL NOT NULL, latitude REAL NOT NULL, "
            "temperature REAL NOT NULL, relative_humidity REAL NOT NULL, "
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
    
    auto statement = prepare(m_database,
        "INSERT OR IGNORE INTO readings(node_id, session_id, seq, longitude, latitude, temperature, relative_humidity) "
        "VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7);");
    
    bind_identity(m_database, statement.get(), reading.identity);
    
    if (sqlite3_bind_double(statement.get(), 4, reading.payload.location.longitude) != SQLITE_OK ||
        sqlite3_bind_double(statement.get(), 5, reading.payload.location.latitude) != SQLITE_OK ||
        sqlite3_bind_double(statement.get(), 6, reading.payload.temperature) != SQLITE_OK ||
        sqlite3_bind_double(statement.get(), 7, reading.payload.relative_humidity) != SQLITE_OK ||
        sqlite3_step(statement.get()) != SQLITE_DONE) {
        database_error(m_database, "Save reading");
    }
    
    if (sqlite3_changes(m_database) == 1) {
        return SaveResult::Inserted;
    }

    return SaveResult::Duplicate;
}

std::optional<common::protocol::Reading> ReadingStore::find(
    const common::protocol::Identity& identity) const {
    auto statement = prepare(m_database,
        "SELECT COUNT(*) FROM readings WHERE node_id=?1 AND session_id=?2 AND seq=?3;");
    
    bind_identity(m_database, statement.get(), identity);
    
    const int result = sqlite3_step(statement.get());
    if (result != SQLITE_ROW) database_error(m_database, "Find reading");
    
    const int count = sqlite3_column_int(statement.get(), 0);
    
    if (count > 0) {
        return common::protocol::Reading{identity, common::protocol::Qos::BestEffort, {}, true};
    }
    return std::nullopt;
}