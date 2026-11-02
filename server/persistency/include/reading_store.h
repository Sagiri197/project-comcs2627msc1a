#pragma once

#include "common/protocol.h"

#include <optional>
#include <stdexcept>
#include <string>

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

    explicit ReadingStore(const std::string& path);
    ~ReadingStore();

    ReadingStore(const ReadingStore&) = delete;
    ReadingStore& operator=(const ReadingStore&) = delete;

    SaveResult save(const common::protocol::Reading& reading);
    std::optional<common::protocol::Reading> find(const common::protocol::Identity& identity) const;

private:
    sqlite3* m_database = nullptr;
};