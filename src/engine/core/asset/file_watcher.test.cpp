#include <gtest/gtest.h>

#include "engine/core/asset/file_watcher.hpp"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <random>
#include <string>
#include <thread>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace std::chrono_literals;
using mmo::engine::core::asset::FileWatcher;

namespace {

fs::path make_temp_dir(const std::string& prefix) {
    static std::atomic<uint32_t> counter{0};
    std::random_device rd;
    auto pid = static_cast<uint32_t>(getpid());
    auto id = counter.fetch_add(1) ^ rd();
    auto p = fs::temp_directory_path() /
             ("mmo_" + prefix + "_" + std::to_string(pid) + "_" + std::to_string(id));
    fs::create_directories(p);
    return p;
}

void write_file(const fs::path& p, const std::string& contents) {
    std::ofstream o(p, std::ios::trunc | std::ios::binary);
    o << contents;
}

// Drain callbacks for up to `budget` iterations of 50ms each, until `predicate`
// becomes true. Returns true on success.
bool drain_until(FileWatcher& w, std::function<bool()> predicate, int budget = 80) {
    for (int i = 0; i < budget; ++i) {
        w.poll_main_thread();
        if (predicate()) return true;
        std::this_thread::sleep_for(50ms);
    }
    return false;
}

class FileWatcherFixture : public ::testing::Test {
protected:
    void SetUp() override {
        // Use 50ms poll interval for snappy tests.
        ASSERT_TRUE(watcher.init(50ms));
    }
    void TearDown() override {
        watcher.shutdown();
    }
    FileWatcher watcher;
};

} // namespace

TEST_F(FileWatcherFixture, FileChangeFiresCallback) {
    auto dir = make_temp_dir("fw_file");
    auto file = dir / "a.txt";
    write_file(file, "hello");
    // Prime mtime cache by yielding past the settle window.
    std::this_thread::sleep_for(150ms);

    std::atomic<int> hits{0};
    auto h = watcher.watch_file(file, [&](const fs::path&) { hits.fetch_add(1); });
    ASSERT_NE(h, FileWatcher::k_invalid_handle);

    // Allow the first scan to prime the cache.
    std::this_thread::sleep_for(150ms);
    watcher.poll_main_thread();
    EXPECT_EQ(hits.load(), 0);

    write_file(file, "modified");
    EXPECT_TRUE(drain_until(watcher, [&] { return hits.load() >= 1; }));

    fs::remove_all(dir);
}

TEST_F(FileWatcherFixture, DirectoryWatchHonoursExtensionFilter) {
    auto dir = make_temp_dir("fw_dir");
    write_file(dir / "init.txt", "x");
    write_file(dir / "init.bin", "y");
    std::this_thread::sleep_for(150ms);

    std::atomic<int> txt_hits{0};
    std::atomic<int> any_hits{0};
    auto h = watcher.watch_directory(dir, ".txt",
        [&](const fs::path& p) {
            any_hits.fetch_add(1);
            if (p.extension() == ".txt") txt_hits.fetch_add(1);
        });
    ASSERT_NE(h, FileWatcher::k_invalid_handle);

    std::this_thread::sleep_for(150ms);
    watcher.poll_main_thread();
    EXPECT_EQ(any_hits.load(), 0);

    write_file(dir / "init.txt", "x2");
    write_file(dir / "init.bin", "y2");

    EXPECT_TRUE(drain_until(watcher, [&] { return txt_hits.load() >= 1; }));
    // The bin file must never produce a callback.
    EXPECT_EQ(any_hits.load(), txt_hits.load());

    fs::remove_all(dir);
}

TEST_F(FileWatcherFixture, UnwatchStopsCallbacks) {
    auto dir = make_temp_dir("fw_unwatch");
    auto file = dir / "f.txt";
    write_file(file, "0");
    std::this_thread::sleep_for(150ms);

    std::atomic<int> hits{0};
    auto h = watcher.watch_file(file, [&](const fs::path&) { hits.fetch_add(1); });
    std::this_thread::sleep_for(150ms);
    watcher.poll_main_thread();

    watcher.unwatch(h);

    write_file(file, "1");
    std::this_thread::sleep_for(400ms);
    watcher.poll_main_thread();
    EXPECT_EQ(hits.load(), 0);

    fs::remove_all(dir);
}

TEST_F(FileWatcherFixture, MultipleWatchersOnSameFileBothFire) {
    auto dir = make_temp_dir("fw_multi");
    auto file = dir / "shared.txt";
    write_file(file, "v0");
    std::this_thread::sleep_for(150ms);

    std::atomic<int> a{0}, b{0};
    watcher.watch_file(file, [&](const fs::path&) { a.fetch_add(1); });
    watcher.watch_file(file, [&](const fs::path&) { b.fetch_add(1); });

    std::this_thread::sleep_for(150ms);
    watcher.poll_main_thread();

    write_file(file, "v1");
    EXPECT_TRUE(drain_until(watcher,
                            [&] { return a.load() >= 1 && b.load() >= 1; }));

    fs::remove_all(dir);
}
