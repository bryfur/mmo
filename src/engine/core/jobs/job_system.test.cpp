#include <gtest/gtest.h>

#include "engine/core/jobs/job_system.hpp"

#include <atomic>
#include <chrono>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace mmo::engine::core::jobs;

namespace {

class JobSystemTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!JobSystem::instance().is_initialized()) {
            JobSystem::instance().init();
        }
    }
};

} // namespace

TEST_F(JobSystemTest, InitIsIdempotent) {
    auto& js = JobSystem::instance();
    const unsigned wc = js.worker_count();
    js.init();
    EXPECT_EQ(js.worker_count(), wc);
    EXPECT_TRUE(js.is_initialized());
    EXPECT_GE(wc, 1u);
}

TEST_F(JobSystemTest, SubmitSingleTaskCompletes) {
    auto& js = JobSystem::instance();
    std::atomic<int> counter{0};
    auto h = js.submit([&]() { counter.fetch_add(1); });
    js.wait(h);
    EXPECT_EQ(counter.load(), 1);
    EXPECT_TRUE(h.is_done());
}

TEST_F(JobSystemTest, SubmitManyTasksAllRun) {
    auto& js = JobSystem::instance();
    constexpr int N = 256;
    std::atomic<int> counter{0};
    std::vector<TaskHandle> handles;
    handles.reserve(N);
    for (int i = 0; i < N; ++i) {
        handles.push_back(js.submit([&]() { counter.fetch_add(1); }));
    }
    for (auto& h : handles) js.wait(h);
    EXPECT_EQ(counter.load(), N);
}

TEST_F(JobSystemTest, WaitAllDrainsEverything) {
    auto& js = JobSystem::instance();
    constexpr int N = 1024;
    std::atomic<int> counter{0};
    for (int i = 0; i < N; ++i) {
        (void)js.submit([&]() { counter.fetch_add(1); });
    }
    js.wait_all();
    EXPECT_EQ(counter.load(), N);
}

TEST_F(JobSystemTest, WaitByHelpingDrainsNestedSubmits) {
    auto& js = JobSystem::instance();
    std::atomic<int> outer{0};
    std::atomic<int> inner{0};

    std::vector<TaskHandle> outer_handles;
    for (int i = 0; i < 16; ++i) {
        outer_handles.push_back(js.submit([&]() {
            outer.fetch_add(1);
            std::vector<TaskHandle> inner_handles;
            for (int k = 0; k < 8; ++k) {
                inner_handles.push_back(js.submit([&]() {
                    inner.fetch_add(1);
                }));
            }
            for (auto& ih : inner_handles) JobSystem::instance().wait(ih);
        }));
    }
    for (auto& h : outer_handles) js.wait(h);
    EXPECT_EQ(outer.load(), 16);
    EXPECT_EQ(inner.load(), 16 * 8);
}

TEST_F(JobSystemTest, ExceptionPropagatesViaWait) {
    auto& js = JobSystem::instance();
    auto h = js.submit([]() { throw std::runtime_error("boom"); });
    EXPECT_THROW(js.wait(h), std::runtime_error);
    EXPECT_TRUE(h.is_done());
    EXPECT_TRUE(h.has_error());
}

TEST_F(JobSystemTest, ExceptionDoesNotKillPool) {
    auto& js = JobSystem::instance();
    auto bad = js.submit([]() { throw std::logic_error("x"); });
    try { js.wait(bad); } catch (...) {}

    std::atomic<int> ok{0};
    auto good = js.submit([&]() { ok.fetch_add(1); });
    js.wait(good);
    EXPECT_EQ(ok.load(), 1);
}

TEST_F(JobSystemTest, StressOneHundredKTasks) {
    auto& js = JobSystem::instance();
    constexpr int N = 100'000;
    std::atomic<int> counter{0};
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < N; ++i) {
        (void)js.submit([&]() { counter.fetch_add(1, std::memory_order_relaxed); });
    }
    js.wait_all();
    auto t1 = std::chrono::steady_clock::now();
    EXPECT_EQ(counter.load(), N);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    EXPECT_LT(ms, 30'000);
}

TEST_F(JobSystemTest, MultiThreadSubmitters) {
    auto& js = JobSystem::instance();
    constexpr int THREADS = 4;
    constexpr int PER_THREAD = 1000;
    std::atomic<int> counter{0};

    std::vector<std::thread> submitters;
    for (int t = 0; t < THREADS; ++t) {
        submitters.emplace_back([&]() {
            std::vector<TaskHandle> handles;
            handles.reserve(PER_THREAD);
            for (int i = 0; i < PER_THREAD; ++i) {
                handles.push_back(js.submit([&]() {
                    counter.fetch_add(1, std::memory_order_relaxed);
                }));
            }
            for (auto& h : handles) JobSystem::instance().wait(h);
        });
    }
    for (auto& t : submitters) t.join();
    EXPECT_EQ(counter.load(), THREADS * PER_THREAD);
}

TEST_F(JobSystemTest, WorkerCountReportedCorrectly) {
    EXPECT_GE(JobSystem::instance().worker_count(), 1u);
}
