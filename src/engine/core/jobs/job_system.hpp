#pragma once

#include "engine/core/jobs/task_handle.hpp"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

namespace mmo::engine::core::jobs {

class JobSystem {
public:
    static JobSystem& instance();

    JobSystem() = default;
    ~JobSystem();

    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    void init(unsigned thread_count = 0);
    void shutdown();

    bool is_initialized() const noexcept { return initialized_.load(std::memory_order_acquire); }
    unsigned worker_count() const noexcept { return static_cast<unsigned>(workers_.size()); }
    unsigned pending_count() const noexcept { return pending_.load(std::memory_order_acquire); }

    TaskHandle submit(std::function<void()> fn);

    void wait(TaskHandle h);
    void wait_all();

private:
    struct Job {
        std::function<void()> fn;
        std::shared_ptr<Task> task;
    };

    struct Worker {
        std::deque<Job> queue;
        std::mutex mutex;
    };

    void worker_main_(unsigned index);
    bool try_pop_local_(unsigned index, Job& out);
    bool try_steal_(unsigned thief, Job& out);
    bool try_pop_global_(Job& out);
    void execute_(Job& job);

    std::vector<std::thread> workers_;
    std::vector<std::unique_ptr<Worker>> worker_queues_;

    std::deque<Job> global_queue_;
    mutable std::mutex global_mutex_;
    std::condition_variable global_cv_;

    std::atomic<bool> initialized_{false};
    std::atomic<bool> stopping_{false};
    std::atomic<unsigned> pending_{0};

    mutable std::mutex wait_all_mutex_;
    std::condition_variable wait_all_cv_;
};

} // namespace mmo::engine::core::jobs
