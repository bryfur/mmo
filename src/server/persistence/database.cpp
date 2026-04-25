#include "database.hpp"
#include "../log.hpp"

#include <string>

namespace mmo::server::persistence {

namespace {

// Bumped each time the schema changes. migrate() applies any missing steps.
constexpr int kCurrentSchemaVersion = 2;

// v1 schema (initial). New installs go straight to v2 via the ALTER step
// below; the v1 baseline is kept here so the migration logic mirrors what
// historical databases looked like.
constexpr const char* kSchemaV1 = R"SQL(
    CREATE TABLE IF NOT EXISTS schema_version (
        version INTEGER PRIMARY KEY
    );

    CREATE TABLE IF NOT EXISTS players (
        name           TEXT PRIMARY KEY,
        player_class   INTEGER NOT NULL,
        level          INTEGER NOT NULL DEFAULT 1,
        xp             INTEGER NOT NULL DEFAULT 0,
        gold           INTEGER NOT NULL DEFAULT 0,
        pos_x          REAL    NOT NULL DEFAULT 0,
        pos_y          REAL    NOT NULL DEFAULT 0,
        pos_z          REAL    NOT NULL DEFAULT 0,
        rotation       REAL    NOT NULL DEFAULT 0,
        health         REAL    NOT NULL,
        max_health     REAL    NOT NULL,
        mana           REAL    NOT NULL DEFAULT 0,
        max_mana       REAL    NOT NULL DEFAULT 0,
        mana_regen     REAL    NOT NULL DEFAULT 0,
        talent_points  INTEGER NOT NULL DEFAULT 0,
        last_seen      INTEGER NOT NULL DEFAULT 0
    );

    CREATE TABLE IF NOT EXISTS player_inventory (
        name     TEXT    NOT NULL,
        slot     INTEGER NOT NULL,
        item_id  TEXT    NOT NULL,
        count    INTEGER NOT NULL,
        PRIMARY KEY (name, slot)
    );

    CREATE TABLE IF NOT EXISTS player_equipment (
        name      TEXT PRIMARY KEY,
        weapon_id TEXT NOT NULL DEFAULT '',
        armor_id  TEXT NOT NULL DEFAULT ''
    );

    CREATE TABLE IF NOT EXISTS player_talents (
        name      TEXT NOT NULL,
        talent_id TEXT NOT NULL,
        PRIMARY KEY (name, talent_id)
    );

    CREATE TABLE IF NOT EXISTS player_completed_quests (
        name     TEXT NOT NULL,
        quest_id TEXT NOT NULL,
        PRIMARY KEY (name, quest_id)
    );

    CREATE TABLE IF NOT EXISTS player_active_quests (
        name             TEXT NOT NULL,
        quest_id         TEXT NOT NULL,
        objectives_json  TEXT NOT NULL,
        PRIMARY KEY (name, quest_id)
    );
)SQL";

[[noreturn]] void throw_db_error(sqlite3* db, const char* what) {
    int code = sqlite3_errcode(db);
    std::string msg = std::string(what) + ": " + sqlite3_errmsg(db);
    throw DbError(msg, code);
}

} // namespace

Database::Database(const std::string& path) {
    int rc = sqlite3_open(path.c_str(), &db_);
    if (rc != SQLITE_OK) {
        std::string msg = "sqlite3_open(" + path + ") failed: ";
        msg += db_ ? sqlite3_errmsg(db_) : sqlite3_errstr(rc);
        if (db_) {
            sqlite3_close(db_);
            db_ = nullptr;
        }
        throw DbError(msg, rc);
    }
    // Sensible defaults. WAL gives concurrent readers + a single writer; we
    // only have one writer (the server), but WAL also fsyncs less often.
    exec("PRAGMA journal_mode = WAL;");
    exec("PRAGMA synchronous = NORMAL;");
    exec("PRAGMA foreign_keys = ON;");
    exec("PRAGMA busy_timeout = 5000;");
}

Database::~Database() {
    close();
}

void Database::close() {
    if (db_) {
        sqlite3_close_v2(db_);
        db_ = nullptr;
    }
}

void Database::exec(std::string_view sql) {
    char* err = nullptr;
    int rc = sqlite3_exec(db_, std::string(sql).c_str(), nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        std::string msg = "sqlite3_exec failed: ";
        msg += err ? err : "unknown";
        if (err) {
            sqlite3_free(err);
        }
        throw DbError(msg, rc);
    }
}

void Database::migrate() {
    exec(kSchemaV1);

    // Read current version (zero rows = first run; insert).
    int version = 0;
    {
        Statement q(*this, "SELECT version FROM schema_version LIMIT 1;");
        if (q.step()) {
            version = q.column_int(0);
        }
    }

    auto bump_version_to = [this](int v) {
        Statement upd(*this, "UPDATE schema_version SET version = ?;");
        upd.bind_int(1, v);
        upd.step();
    };

    if (version == 0) {
        // Brand-new database — record schema_version row, then apply every
        // post-baseline migration so a fresh install matches an upgraded one.
        Statement ins(*this, "INSERT INTO schema_version (version) VALUES (?);");
        ins.bind_int(1, 1);
        ins.step();
        version = 1;
        LOG_INFO("DB") << "Initialized schema version " << version;
    }

    // v1 -> v2: add players.was_dead flag (default 0 = alive).
    if (version < 2) {
        // SQLite ADD COLUMN is non-atomic w.r.t. other connections, but the
        // server is the only writer so this is safe.
        exec("ALTER TABLE players ADD COLUMN was_dead INTEGER NOT NULL DEFAULT 0;");
        bump_version_to(2);
        version = 2;
        LOG_INFO("DB") << "Migrated schema to version " << version;
    }

    if (version < kCurrentSchemaVersion) {
        LOG_ERROR("DB") << "Schema version " << version << " < current " << kCurrentSchemaVersion
                        << " — missing migration step. Refusing to continue.";
        throw DbError("missing schema migration", -1);
    }
}

void Database::begin() {
    exec("BEGIN;");
}
void Database::commit() {
    exec("COMMIT;");
}
void Database::rollback() {
    exec("ROLLBACK;");
}

// -------------------- Statement --------------------

Statement::Statement(Database& db, std::string_view sql) : db_(db) {
    int rc = sqlite3_prepare_v2(db.handle(), sql.data(), static_cast<int>(sql.size()), &stmt_, nullptr);
    if (rc != SQLITE_OK) {
        throw_db_error(db.handle(), "sqlite3_prepare_v2");
    }
}

Statement::~Statement() {
    if (stmt_) {
        sqlite3_finalize(stmt_);
    }
}

void Statement::reset() {
    sqlite3_reset(stmt_);
    sqlite3_clear_bindings(stmt_);
}

bool Statement::step() {
    int rc = sqlite3_step(stmt_);
    if (rc == SQLITE_ROW) {
        return true;
    }
    if (rc == SQLITE_DONE) {
        return false;
    }
    throw_db_error(db_.handle(), "sqlite3_step");
}

void Statement::bind_int(int idx, int value) {
    sqlite3_bind_int(stmt_, idx, value);
}
void Statement::bind_int64(int idx, sqlite3_int64 value) {
    sqlite3_bind_int64(stmt_, idx, value);
}
void Statement::bind_double(int idx, double value) {
    sqlite3_bind_double(stmt_, idx, value);
}
void Statement::bind_null(int idx) {
    sqlite3_bind_null(stmt_, idx);
}
void Statement::bind_text(int idx, std::string_view value) {
    sqlite3_bind_text(stmt_, idx, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT);
}

int Statement::column_int(int idx) const {
    return sqlite3_column_int(stmt_, idx);
}
sqlite3_int64 Statement::column_int64(int idx) const {
    return sqlite3_column_int64(stmt_, idx);
}
double Statement::column_double(int idx) const {
    return sqlite3_column_double(stmt_, idx);
}
std::string Statement::column_text(int idx) const {
    const unsigned char* p = sqlite3_column_text(stmt_, idx);
    int n = sqlite3_column_bytes(stmt_, idx);
    return p ? std::string(reinterpret_cast<const char*>(p), n) : std::string{};
}

} // namespace mmo::server::persistence
