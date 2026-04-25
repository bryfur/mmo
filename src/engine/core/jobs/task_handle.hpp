#pragma once

#include <atomic>
#include <exception>
#include <memory>

namespace mmo::engine::core::jobs {

struct Task {
    std::atomic<bool> done{false};
    std::exception_ptr error;
};

class TaskHandle {
public:
    TaskHandle() = default;
    explicit TaskHandle(std::shared_ptr<Task> t) : task_(std::move(t)) {}

    bool valid() const noexcept { return static_cast<bool>(task_); }
    bool is_done() const noexcept { return task_ && task_->done.load(std::memory_order_acquire); }
    bool has_error() const noexcept {
        return task_ && task_->done.load(std::memory_order_acquire) && static_cast<bool>(task_->error);
    }
    std::exception_ptr error() const noexcept { return task_ ? task_->error : nullptr; }

    const std::shared_ptr<Task>& raw() const noexcept { return task_; }

private:
    std::shared_ptr<Task> task_;
};

} // namespace mmo::engine::core::jobs
