#include <gtest/gtest.h>

#include "engine/core/logger.hpp"

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace mmo::engine::core;

namespace {

class CapturingSink : public LogSink {
public:
    struct Entry {
        LogLevel level;
        std::string category;
        std::string msg;
    };

    void write(LogLevel level, const char* category, std::string_view msg) override {
        std::lock_guard<std::mutex> lock(mutex_);
        entries.push_back(Entry{ level, std::string(category ? category : ""), std::string(msg) });
    }

    std::mutex mutex_;
    std::vector<Entry> entries;
};

struct LoggerFixture : ::testing::Test {
    void SetUp() override {
        Logger::instance().clear_sinks();
        Logger::instance().set_min_level(LogLevel::Trace);
        sink = std::make_shared<CapturingSink>();
        handle = Logger::instance().add_sink(sink);
    }
    void TearDown() override {
        Logger::instance().remove_sink(handle);
        Logger::instance().clear_sinks();
        Logger::instance().add_sink(std::make_shared<StdoutSink>());
        Logger::instance().set_min_level(LogLevel::Info);
    }
    std::shared_ptr<CapturingSink> sink;
    Logger::SinkHandle handle = 0;
};

} // namespace

TEST_F(LoggerFixture, SinkReceivesFormattedMessage) {
    ENGINE_LOG_INFO("render", "hello {} {}", "world", 42);
    ASSERT_EQ(sink->entries.size(), 1u);
    EXPECT_EQ(sink->entries[0].level, LogLevel::Info);
    EXPECT_EQ(sink->entries[0].category, "render");
    EXPECT_EQ(sink->entries[0].msg, "hello world 42");
}

TEST_F(LoggerFixture, LevelFilteringDropsLowSeverity) {
    Logger::instance().set_min_level(LogLevel::Warn);
    ENGINE_LOG_TRACE("a", "trace");
    ENGINE_LOG_DEBUG("a", "debug");
    ENGINE_LOG_INFO("a", "info");
    ENGINE_LOG_WARN("a", "warn");
    ENGINE_LOG_ERROR("a", "error");
    ASSERT_EQ(sink->entries.size(), 2u);
    EXPECT_EQ(sink->entries[0].level, LogLevel::Warn);
    EXPECT_EQ(sink->entries[1].level, LogLevel::Error);
}

TEST_F(LoggerFixture, OffSilencesEverything) {
    Logger::instance().set_min_level(LogLevel::Off);
    ENGINE_LOG_FATAL("a", "fatal");
    EXPECT_EQ(sink->entries.size(), 0u);
}

TEST_F(LoggerFixture, RemoveSinkStopsDelivery) {
    Logger::instance().remove_sink(handle);
    ENGINE_LOG_INFO("a", "x");
    EXPECT_EQ(sink->entries.size(), 0u);
}

TEST_F(LoggerFixture, MultipleSinksReceiveSameMessage) {
    auto second = std::make_shared<CapturingSink>();
    auto h2 = Logger::instance().add_sink(second);
    ENGINE_LOG_INFO("a", "broadcast");
    EXPECT_EQ(sink->entries.size(), 1u);
    EXPECT_EQ(second->entries.size(), 1u);
    Logger::instance().remove_sink(h2);
}

TEST_F(LoggerFixture, ThreadSafeConcurrentLogging) {
    constexpr int kThreads = 8;
    constexpr int kPerThread = 200;
    std::vector<std::thread> ts;
    std::atomic<int> ready{0};
    for (int i = 0; i < kThreads; ++i) {
        ts.emplace_back([&, i]() {
            ready.fetch_add(1);
            while (ready.load() < kThreads) {}
            for (int j = 0; j < kPerThread; ++j) {
                ENGINE_LOG_INFO("thread", "t={} j={}", i, j);
            }
        });
    }
    for (auto& t : ts) t.join();
    EXPECT_EQ(sink->entries.size(), static_cast<size_t>(kThreads * kPerThread));
}
