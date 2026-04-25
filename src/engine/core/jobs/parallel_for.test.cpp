#include <gtest/gtest.h>

#include "engine/core/jobs/job_system.hpp"
#include "engine/core/jobs/parallel_for.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <numeric>
#include <vector>

using namespace mmo::engine::core::jobs;

namespace {

class ParallelForTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!JobSystem::instance().is_initialized()) {
            JobSystem::instance().init();
        }
    }
};

} // namespace

TEST_F(ParallelForTest, EmptyRangeIsNoop) {
    int touched = 0;
    parallel_for(5, 5, [&](std::size_t, std::size_t) { ++touched; });
    EXPECT_EQ(touched, 0);
}

TEST_F(ParallelForTest, FillsTenThousandIndices) {
    constexpr std::size_t N = 10'000;
    std::vector<int> data(N, 0);
    parallel_for(0, N, [&](std::size_t b, std::size_t e) {
        for (std::size_t i = b; i < e; ++i) data[i] = static_cast<int>(i);
    });
    long long sum = std::accumulate(data.begin(), data.end(), 0LL);
    long long expected = static_cast<long long>(N) * (N - 1) / 2;
    EXPECT_EQ(sum, expected);
}

TEST_F(ParallelForTest, SerialFallbackForLargeGrain) {
    constexpr std::size_t N = 1000;
    std::atomic<int> chunks{0};
    parallel_for(0, N, [&](std::size_t, std::size_t) { chunks.fetch_add(1); }, /*grain=*/N * 2);
    EXPECT_EQ(chunks.load(), 1);
}

TEST_F(ParallelForTest, AutomaticGrainSplitsRange) {
    auto& js = JobSystem::instance();
    if (js.worker_count() < 2) {
        GTEST_SKIP() << "Need at least 2 workers";
    }
    constexpr std::size_t N = 4096;
    std::atomic<int> chunks{0};
    parallel_for(0, N, [&](std::size_t, std::size_t) { chunks.fetch_add(1); });
    EXPECT_GT(chunks.load(), 1);
}

TEST_F(ParallelForTest, AccumulatesAtomicCounter) {
    constexpr std::size_t N = 50'000;
    std::atomic<long long> sum{0};
    parallel_for(0, N, [&](std::size_t b, std::size_t e) {
        long long local = 0;
        for (std::size_t i = b; i < e; ++i) local += static_cast<long long>(i);
        sum.fetch_add(local, std::memory_order_relaxed);
    });
    long long expected = static_cast<long long>(N) * (N - 1) / 2;
    EXPECT_EQ(sum.load(), expected);
}

TEST_F(ParallelForTest, StressOneThousandTrivialTasksAllRun) {
    constexpr std::size_t N = 1000;
    std::atomic<int> seen{0};
    parallel_for(
        0, N,
        [&](std::size_t b, std::size_t e) {
            for (std::size_t i = b; i < e; ++i) seen.fetch_add(1, std::memory_order_relaxed);
        },
        /*grain=*/1);
    EXPECT_EQ(seen.load(), static_cast<int>(N));
}

TEST_F(ParallelForTest, PendingCountIsZeroOnceDrained) {
    constexpr std::size_t N = 256;
    parallel_for(0, N, [](std::size_t, std::size_t) {});
    EXPECT_EQ(JobSystem::instance().pending_count(), 0u);
}

TEST_F(ParallelForTest, BusySpinIsFasterThanSerial) {
    auto& js = JobSystem::instance();
    if (js.worker_count() < 2) {
        GTEST_SKIP() << "Need at least 2 workers";
    }
    constexpr std::size_t N = 64;

    auto busy_chunk = [](std::size_t b, std::size_t e) {
        volatile std::uint64_t acc = 0;
        for (std::size_t i = b; i < e; ++i) {
            for (int k = 0; k < 200'000; ++k) {
                acc += static_cast<std::uint64_t>(i) * k;
            }
        }
        (void)acc;
    };

    auto t0 = std::chrono::steady_clock::now();
    busy_chunk(0, N);
    auto t1 = std::chrono::steady_clock::now();
    auto serial_us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();

    auto p0 = std::chrono::steady_clock::now();
    parallel_for(0, N, busy_chunk, /*grain=*/1);
    auto p1 = std::chrono::steady_clock::now();
    auto par_us = std::chrono::duration_cast<std::chrono::microseconds>(p1 - p0).count();

    EXPECT_LT(par_us, serial_us);
}
