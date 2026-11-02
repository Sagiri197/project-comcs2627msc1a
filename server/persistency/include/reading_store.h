#pragma once

#include "common/protocol.h"

#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

struct sqlite3;

class PersistenceError : public std::runtime_error {
public:
    PersistenceError(int code, const std::string& message);
    int code() const noexcept;

private:
    int m_code;
};

class ReadingStore {
public:
    enum class SaveResult { Inserted, Duplicate, IdentityConflict };

    struct StoredReading {
        common::protocol::Reading reading;
        bool processed;
    };

    explicit ReadingStore(const std::string& path);
    ~ReadingStore();

    ReadingStore(const ReadingStore&) = delete;
    ReadingStore& operator=(const ReadingStore&) = delete;

    SaveResult save(const common::protocol::Reading& reading);
    std::optional<StoredReading> find(const common::protocol::Identity& identity) const;
    std::vector<common::protocol::Reading> pending(std::size_t limit = 100) const;
    bool mark_processed(const common::protocol::Identity& identity);

private:
    sqlite3* m_database = nullptr;
};