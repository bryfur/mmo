#include "engine/core/jobs/job_system.hpp"

#include "engine/core/logger.hpp"
#include "engine/core/profiler.hpp"

#include <algorithm>
#include <chrono>

namespace mmo::engine::core::jobs {

namespace {

thread_local int t_worker_index_ = -1;

} // namespace

JobSystem& JobSystem::instance() {
    static JobSystem inst;
    return inst;
}

JobSystem::~JobSystem() {
    shutdown();
}

void JobSystem::init(unsigned thread_count) {
    if (initialized_.load(std::memory_order_acquire)) return;

    unsigned n = thread_count;
    if (n == 0) {
        const unsigned hw = std::thread::hardware_concurrency();
        n = hw > 1 ? hw - 1 : 1;
    }
    n = std::max(1u, n);

    stopping_.store(false, std::memory_order_release);
    pending_.store(0, std::memory_order_release);

    worker_queues_.clear();
    worker_queues_.reserve(n);
    for (unsigned i = 0; i < n; ++i) {
        worker_queues_.emplace_back(std::make_unique<Worker>());
    }

    workers_.reserve(n);
    for (unsigned i = 0; i < n; ++i) {
        workers_.emplace_back([this, i]() { worker_main_(i); });
    }

    initialized_.store(true, std::memory_order_release);
    ENGINE_LOG_INFO("jobs", "Initialized {} workers", n);
}

void JobSystem::shutdown() {
    if (!initialized_.load(std::memory_order_acquire)) return;

    stopping_.store(true, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(global_mutex_);
    }
    global_cv_.notify_all();

    for (auto& t : workers_) {
        if (t.joinable()) t.join();
    }
    workers_.clear();

    {
        std::lock_guard<std::mutex> lock(global_mutex_);
        global_queue_.clear();
    }
    for (auto& w : worker_queues_) {
        std::lock_guard<std::mutex> lock(w->mutex);
        w->queue.clear();
    }
    worker_queues_.clear();

    pending_.store(0, std::memory_order_release);
    initialized_.store(false, std::memory_order_release);
    stopping_.store(false, std::memory_order_release);
}

TaskHandle JobSystem::submit(std::function<void()> fn) {
    auto task = std::make_shared<Task>();
    Job job{std::move(fn), task};
    pending_.fetch_add(1, std::memory_order_acq_rel);

    const int wi = t_worker_index_;
    if (wi >= 0 && static_cast<size_t>(wi) < worker_queues_.size()) {
        auto& w = *worker_queues_[wi];
        std::lock_guard<std::mutex> lock(w.mutex);
        w.queue.push_back(std::move(job));
    } else {
        std::lock_guard<std::mutex> lock(global_mutex_);
        global_queue_.push_back(std::move(job));
    }
    global_cv_.notify_one();
    return TaskHandle(task);
}

bool JobSystem::try_pop_local_(unsigned index, Job& out) {
    if (index >= worker_queues_.size()) return false;
    auto& w = *worker_queues_[index];
    std::lock_guard<std::mutex> lock(w.mutex);
    if (w.queue.empty()) return false;
    out = std::move(w.queue.back());
    w.queue.pop_back();
    return true;
}

bool JobSystem::try_steal_(unsigned thief, Job& out) {
    const size_t n = worker_queues_.size();
    if (n <= 1) return false;
    thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<size_t> dist(0, n - 1);
    for (size_t attempts = 0; attempts < n * 2; ++attempts) {
        size_t victim = dist(rng);
        if (victim == thief) continue;
        auto& w = *worker_queues_[victim];
        std::unique_lock<std::mutex> lock(w.mutex, std::try_to_lock);
        if (!lock.owns_lock()) continue;
        if (w.queue.empty()) continue;
        out = std::move(w.queue.front());
        w.queue.pop_front();
        return true;
    }
    return false;
}

bool JobSystem::try_pop_global_(Job& out) {
    std::lock_guard<std::mutex> lock(global_mutex_);
    if (global_queue_.empty()) return false;
    out = std::move(global_queue_.front());
    global_queue_.pop_front();
    return true;
}

void JobSystem::execute_(Job& job) {
    try {
        job.fn();
    } catch (...) {
        if (job.task) job.task->error = std::current_exception();
    }
    if (job.task) {
        job.task->done.store(true, std::memory_order_release);
    }
    const unsigned prev = pending_.fetch_sub(1, std::memory_order_acq_rel);
    if (prev == 1) {
        std::lock_guard<std::mutex> lock(wait_all_mutex_);
        wait_all_cv_.notify_all();
    }
}

void JobSystem::worker_main_(unsigned index) {
    ENGINE_PROFILE_ZONE("JobWorker");
    t_worker_index_ = static_cast<int>(index);

    auto last_plot = std::chrono::steady_clock::now();
    while (!stopping_.load(std::memory_order_acquire)) {
        Job job;
        if (try_pop_local_(index, job) ||
            try_pop_global_(job) ||
            try_steal_(index, job)) {
            execute_(job);
            const auto now = std::chrono::steady_clock::now();
            if (index == 0 &&
                std::chrono::duration_cast<std::chrono::seconds>(now - last_plot).count() >= 1) {
                ENGINE_PROFILE_PLOT("jobs.pending",
                    static_cast<int64_t>(pending_.load(std::memory_order_acquire)));
                last_plot = now;
            }
            continue;
        }

        std::unique_lock<std::mutex> lock(global_mutex_);
        global_cv_.wait_for(lock, std::chrono::milliseconds(50), [this]() {
            return stopping_.load(std::memory_order_acquire) || !global_queue_.empty();
        });
    }
}

void JobSystem::wait(TaskHandle h) {
    if (!h.valid()) return;
    auto& task = *h.raw();
    while (!task.done.load(std::memory_order_acquire)) {
        Job job;
        if (try_pop_global_(job)) {
            execute_(job);
            continue;
        }
        if (t_worker_index_ < 0) {
            if (try_steal_(static_cast<unsigned>(-1), job)) {
                execute_(job);
                continue;
            }
        }
        std::this_thread::yield();
    }
    if (task.error) {
        std::rethrow_exception(task.error);
    }
}

void JobSystem::wait_all() {
    while (pending_.load(std::memory_order_acquire) > 0) {
        Job job;
        if (try_pop_global_(job)) {
            execute_(job);
            continue;
        }
        if (t_worker_index_ < 0) {
            if (try_steal_(static_cast<unsigned>(-1), job)) {
                execute_(job);
                continue;
            }
        }
        std::unique_lock<std::mutex> lock(wait_all_mutex_);
        wait_all_cv_.wait_for(lock, std::chrono::milliseconds(50), [this]() {
            return pending_.load(std::memory_order_acquire) == 0;
        });
    }
}

} // namespace mmo::engine::core::jobs
