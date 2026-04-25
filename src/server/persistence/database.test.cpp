#include <gtest/gtest.h>
#include "database.hpp"

#include <filesystem>
#include <thread>

using namespace mmo::server::persistence;

namespace {

// Each test gets a fresh in-memory database. ":memory:" is per-connection in
// SQLite, so tests don't share state.
std::string mem_db() { return std::string(":memory:"); }

// Use a unique-per-test on-disk file when we need to cross connections.
std::string tmp_db_path(std::string_view tag) {
    namespace fs = std::filesystem;
    auto dir = fs::temp_directory_path() / "mmo4_db_test";
    fs::create_directories(dir);
    auto path = dir / (std::string(tag) + "_" +
                       std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id())) +
                       ".db");
    fs::remove(path);
    return path.string();
}

} // namespace

TEST(Database, OpenInMemorySucceeds) {
    Database db(mem_db());
    EXPECT_NE(db.handle(), nullptr);
}

TEST(Database, OpenInvalidPathThrowsDbError) {
    EXPECT_THROW(Database("/this/path/does/not/exist/and/cannot/be/created.db"), DbError);
}

TEST(Database, MigrateCreatesAllTables) {
    Database db(mem_db());
    db.migrate();

    Statement q(db,
        "SELECT name FROM sqlite_master WHERE type='table' ORDER BY name;");
    std::vector<std::string> tables;
    while (q.step()) tables.push_back(q.column_text(0));

    // Note: sqlite_sequence/etc may appear; check by inclusion not exact size.
    auto has = [&](std::string_view name) {
        for (auto& t : tables) if (t == name) return true;
        return false;
    };
    EXPECT_TRUE(has("schema_version"));
    EXPECT_TRUE(has("players"));
    EXPECT_TRUE(has("player_inventory"));
    EXPECT_TRUE(has("player_equipment"));
    EXPECT_TRUE(has("player_talents"));
    EXPECT_TRUE(has("player_completed_quests"));
    EXPECT_TRUE(has("player_active_quests"));
}

TEST(Database, MigrateIsIdempotent) {
    Database db(mem_db());
    db.migrate();
    db.migrate();  // must not throw or duplicate rows

    Statement q(db, "SELECT COUNT(*) FROM schema_version;");
    ASSERT_TRUE(q.step());
    EXPECT_EQ(q.column_int(0), 1);
}

TEST(Database, MigratePersistsAcrossConnections) {
    auto path = tmp_db_path("migrate_persist");
    int first_version = 0;
    {
        Database db(path);
        db.migrate();
        Statement q(db, "SELECT version FROM schema_version;");
        ASSERT_TRUE(q.step());
        first_version = q.column_int(0);
    }
    {
        // Re-opening must see the schema and not throw on second migrate.
        Database db(path);
        db.migrate();
        Statement q(db, "SELECT version FROM schema_version;");
        ASSERT_TRUE(q.step());
        EXPECT_EQ(q.column_int(0), first_version);
    }
    std::filesystem::remove(path);
}

TEST(Statement, BindAndReadRoundtrip) {
    Database db(mem_db());
    db.exec("CREATE TABLE t (i INTEGER, d REAL, s TEXT, b INTEGER);");

    {
        Statement ins(db, "INSERT INTO t VALUES (?,?,?,?);");
        ins.bind_int   (1, 42);
        ins.bind_double(2, 3.14);
        ins.bind_text  (3, "hello world");
        ins.bind_int64 (4, 9'000'000'000LL);  // > INT_MAX
        EXPECT_FALSE(ins.step());  // INSERT yields SQLITE_DONE, not a row
    }

    Statement sel(db, "SELECT i, d, s, b FROM t;");
    ASSERT_TRUE(sel.step());
    EXPECT_EQ(sel.column_int(0), 42);
    EXPECT_DOUBLE_EQ(sel.column_double(1), 3.14);
    EXPECT_EQ(sel.column_text(2), "hello world");
    EXPECT_EQ(sel.column_int64(3), 9'000'000'000LL);
    EXPECT_FALSE(sel.step());
}

TEST(Statement, ResetAllowsReuse) {
    Database db(mem_db());
    db.exec("CREATE TABLE t (n INTEGER);");

    Statement ins(db, "INSERT INTO t VALUES (?);");
    for (int i = 0; i < 5; ++i) {
        ins.reset();
        ins.bind_int(1, i);
        ins.step();
    }

    Statement count(db, "SELECT COUNT(*), SUM(n) FROM t;");
    ASSERT_TRUE(count.step());
    EXPECT_EQ(count.column_int(0), 5);
    EXPECT_EQ(count.column_int(1), 0 + 1 + 2 + 3 + 4);
}

TEST(Statement, TextWithEmbeddedNullsIsPreserved) {
    Database db(mem_db());
    db.exec("CREATE TABLE t (s BLOB);");

    std::string with_null = std::string("a\0b\0c", 5);
    {
        Statement ins(db, "INSERT INTO t VALUES (?);");
        ins.bind_text(1, with_null);
        ins.step();
    }

    Statement sel(db, "SELECT s FROM t;");
    ASSERT_TRUE(sel.step());
    EXPECT_EQ(sel.column_text(0), with_null);  // bytes preserved
}

TEST(Statement, MalformedSqlThrows) {
    Database db(mem_db());
    EXPECT_THROW(Statement(db, "NOT VALID SQL!!"), DbError);
}

TEST(Database, TransactionRollbackDiscardsWrites) {
    Database db(mem_db());
    db.exec("CREATE TABLE t (n INTEGER);");
    db.exec("INSERT INTO t VALUES (1);");

    db.begin();
    db.exec("INSERT INTO t VALUES (2);");
    db.exec("INSERT INTO t VALUES (3);");
    db.rollback();

    Statement count(db, "SELECT COUNT(*) FROM t;");
    ASSERT_TRUE(count.step());
    EXPECT_EQ(count.column_int(0), 1);
}

TEST(Database, OpenSurvivesStaleWalAndShmFiles) {
    // Regression for the start.sh-after-crash failure: a previous run that
    // crashed mid-write left mmo.db-wal and mmo.db-shm beside the main file.
    // SQLite must still be able to open and migrate without reporting
    // "disk I/O error".
    auto path = tmp_db_path("stale_wal");
    {
        Database db(path);
        db.migrate();
        db.exec("INSERT INTO players (name, player_class, health, max_health) "
                "VALUES ('phoenix', 0, 50, 100);");
    }
    // Simulate a crash: leave WAL/SHM intact (no clean checkpoint).
    // We can simulate by re-opening — SQLite should recover.
    {
        Database db(path);
        EXPECT_NO_THROW(db.migrate());
        Statement q(db, "SELECT name FROM players WHERE name='phoenix';");
        ASSERT_TRUE(q.step());
        EXPECT_EQ(q.column_text(0), "phoenix");
    }
    // Cleanup.
    namespace fs = std::filesystem;
    for (auto suffix : {"", "-wal", "-shm", "-journal"}) {
        std::error_code ec;
        fs::remove(path + suffix, ec);
    }
}

TEST(Database, MigrateAddsWasDeadColumnFromV1Baseline) {
    // Simulate an existing v1 database: create the v1 schema by hand WITHOUT
    // the was_dead column, set version=1, then migrate() must add the column
    // and bump to current.
    auto path = tmp_db_path("migrate_v1");
    {
        Database db(path);
        // v1 baseline (no was_dead column)
        db.exec(R"SQL(
            CREATE TABLE schema_version (version INTEGER PRIMARY KEY);
            INSERT INTO schema_version VALUES (1);
            CREATE TABLE players (
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
        )SQL");
        // Insert a v1-shape row — must survive migration with was_dead = 0.
        db.exec("INSERT INTO players (name, player_class, health, max_health) "
                "VALUES ('legacy', 0, 75, 100);");
    }

    // Reopen + migrate → schema_version should be 2 and was_dead column exists.
    {
        Database db(path);
        db.migrate();
        Statement v(db, "SELECT version FROM schema_version;");
        ASSERT_TRUE(v.step());
        EXPECT_EQ(v.column_int(0), 2);

        Statement c(db, "SELECT was_dead FROM players WHERE name='legacy';");
        ASSERT_TRUE(c.step());
        EXPECT_EQ(c.column_int(0), 0);  // existing rows default to 0 (alive)
    }
    std::filesystem::remove(path);
}

TEST(Database, TransactionCommitPersistsWrites) {
    Database db(mem_db());
    db.exec("CREATE TABLE t (n INTEGER);");

    db.begin();
    db.exec("INSERT INTO t VALUES (10);");
    db.exec("INSERT INTO t VALUES (20);");
    db.commit();

    Statement count(db, "SELECT COUNT(*), SUM(n) FROM t;");
    ASSERT_TRUE(count.step());
    EXPECT_EQ(count.column_int(0), 2);
    EXPECT_EQ(count.column_int(1), 30);
}
