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
        "SELECT temperature, relative_humidity, longitude, latitude "
        "FROM readings WHERE node_id=?1 AND session_id=?2 AND seq=?3;");
    
    bind_identity(m_database, statement.get(), identity);
    
    const int result = sqlite3_step(statement.get());
    if (result == SQLITE_ROW) {
        const double temperature = sqlite3_column_double(statement.get(), 0);
        const double humidity = sqlite3_column_double(statement.get(), 1);
        const double longitude = sqlite3_column_double(statement.get(), 2);
        const double latitude = sqlite3_column_double(statement.get(), 3);
        
        common::protocol::WeatherObserved payload{
            "urn:ngsi-ld:WeatherObserved:" + identity.node_id + ":" + identity.session_id + ":" + std::to_string(identity.seq),
            "2026-11-07T00:00:00Z",
            {longitude, latitude},
            "urn:ngsi-ld:Device:" + identity.node_id,
            temperature,
            humidity
        };
        
        return common::protocol::Reading{identity, common::protocol::Qos::BestEffort, payload, true};
    }
    if (result != SQLITE_DONE) database_error(m_database, "Find reading");
    return std::nullopt;
}

std::optional<common::protocol::Reading> ReadingStore::getLastReading() const {
    auto statement = prepare(m_database,
        "SELECT node_id, session_id, seq, temperature, relative_humidity, longitude, latitude "
        "FROM readings ORDER BY seq DESC LIMIT 1;");
    
    const int result = sqlite3_step(statement.get());
    if (result != SQLITE_ROW) {
        return std::nullopt;  // No readings yet
    }
    
    const std::string node_id = reinterpret_cast<const char*>(sqlite3_column_text(statement.get(), 0));
    const std::string session_id = reinterpret_cast<const char*>(sqlite3_column_text(statement.get(), 1));
    const std::uint32_t seq = sqlite3_column_int(statement.get(), 2);
    const double temperature = sqlite3_column_double(statement.get(), 3);
    const double humidity = sqlite3_column_double(statement.get(), 4);
    const double longitude = sqlite3_column_double(statement.get(), 5);
    const double latitude = sqlite3_column_double(statement.get(), 6);
    
    common::protocol::Identity identity{node_id, session_id, seq};
    common::protocol::WeatherObserved payload{
        "urn:ngsi-ld:WeatherObserved:" + node_id + ":" + session_id + ":" + std::to_string(seq),
        "2026-11-07T00:00:00Z",
        {longitude, latitude},
        "urn:ngsi-ld:Device:" + node_id,
        temperature,
        humidity
    };
    
    return common::protocol::Reading{identity, common::protocol::Qos::BestEffort, payload, true};
}