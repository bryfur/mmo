#pragma once

#include <sqlite3.h>
#include <stdexcept>
#include <string>
#include <string_view>

namespace mmo::server::persistence {

class DbError : public std::runtime_error {
public:
    DbError(const std::string& msg, int code) : std::runtime_error(msg), code_(code) {}
    int code() const { return code_; }

private:
    int code_;
};

// Thin RAII wrapper around sqlite3*. Owns the connection; non-copyable, movable.
// Use Statement (below) for query execution; Database itself only opens, runs
// schema migrations, and exposes the raw handle for repositories.
class Database {
public:
    explicit Database(const std::string& path);
    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;
    Database(Database&& other) noexcept : db_(other.db_) { other.db_ = nullptr; }
    Database& operator=(Database&& other) noexcept {
        if (this != &other) {
            close();
            db_ = other.db_;
            other.db_ = nullptr;
        }
        return *this;
    }

    sqlite3* handle() { return db_; }

    /// Execute a one-shot SQL statement (no parameter binding, no rows expected).
    /// Throws DbError on failure.
    void exec(std::string_view sql);

    /// Run schema migrations idempotently. Safe to call on every server start.
    void migrate();

    /// Begin / commit / rollback for batch saves.
    void begin();
    void commit();
    void rollback();

private:
    void close();
    sqlite3* db_ = nullptr;
};

// RAII for a prepared statement. Reset+rebind for reuse, or destruct to finalize.
class Statement {
public:
    Statement(Database& db, std::string_view sql);
    ~Statement();

    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    sqlite3_stmt* handle() { return stmt_; }

    void reset(); // SQLITE_OK on already-bound statements; clears bindings.
    bool step();  // returns true if a row is available; false at end.

    // Bind by 1-based index (SQLite convention).
    void bind_int(int idx, int value);
    void bind_int64(int idx, sqlite3_int64 value);
    void bind_double(int idx, double value);
    void bind_text(int idx, std::string_view value);
    void bind_null(int idx);

    // Column access by 0-based index. Caller must ensure step() returned true.
    int column_int(int idx) const;
    sqlite3_int64 column_int64(int idx) const;
    double column_double(int idx) const;
    std::string column_text(int idx) const;

private:
    sqlite3_stmt* stmt_ = nullptr;
    Database& db_;
};

} // namespace mmo::server::persistence
