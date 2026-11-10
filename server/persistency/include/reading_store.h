#pragma once

#include "common/protocol.h"

#include <optional>
#include <stdexcept>
#include <string>

struct sqlite3;

class PersistenceError : public std::runtime_error {
public:
    /** @brief Creates a persistence error with the underlying SQLite result code. */
    PersistenceError(int code, const std::string& message);

    /** @brief Returns the SQLite result code associated with the failure. */
    int code() const noexcept;

private:
    int m_code;
};

class ReadingStore {
public:
    enum class SaveResult { Inserted, Duplicate };

    explicit ReadingStore(const std::string& path);
    ~ReadingStore();

    ReadingStore(const ReadingStore&) = delete;
    ReadingStore& operator=(const ReadingStore&) = delete;

    /**
     * @brief Saves a reading unless its identity is already present.
     * @return Whether the reading was inserted or was an existing duplicate.
     * @throws PersistenceError if the database operation fails.
     */
    SaveResult save(const common::protocol::Reading& reading);

    /** @brief Finds a reading by its protocol identity, or returns `std::nullopt`. */
    std::optional<common::protocol::Reading> find(const common::protocol::Identity& identity) const;

    /** @brief Returns the most recently stored reading, or `std::nullopt` if empty. */
    std::optional<common::protocol::Reading> getLastReading() const;

private:
    sqlite3* m_database = nullptr;
};