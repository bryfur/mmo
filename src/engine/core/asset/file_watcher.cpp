#include "engine/core/asset/file_watcher.hpp"

#include "engine/core/logger.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iterator>
#include <system_error>
#include <utility>

namespace mmo::engine::core::asset {

namespace fs = std::filesystem;

namespace {
// File mtime is considered settled once it has been quiet for at least this long.
// Avoids reacting to half-written files mid-save.
constexpr int64_t k_settle_window_ns = 100 * 1'000'000;
} // namespace

FileWatcher::~FileWatcher() {
    shutdown();
}

FileWatcher& FileWatcher::instance() {
    static FileWatcher s;
    return s;
}

bool FileWatcher::init(std::chrono::milliseconds poll_interval) {
    if (initialized_.load(std::memory_order_acquire)) {
        return false;
    }
    poll_interval_ = poll_interval;
    stop_.store(false, std::memory_order_release);
    initialized_.store(true, std::memory_order_release);
    worker_ = std::jthread([this](std::stop_token) { worker_loop(); });
    ENGINE_LOG_INFO("hot_reload", "FileWatcher initialized (poll {} ms)",
                    static_cast<long long>(poll_interval_.count()));
    return true;
}

void FileWatcher::shutdown() {
    if (!initialized_.exchange(false, std::memory_order_acq_rel)) {
        return;
    }
    stop_.store(true, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lk(wake_mutex_);
        wake_cv_.notify_all();
    }
    if (worker_.joinable()) {
        worker_.request_stop();
        worker_.join();
    }
    {
        std::lock_guard<std::mutex> lk(watches_mutex_);
        watches_.clear();
    }
    {
        std::lock_guard<std::mutex> lk(queue_mutex_);
        pending_.clear();
    }
    ENGINE_LOG_INFO("hot_reload", "FileWatcher shutdown");
}

FileWatcher::WatchHandle FileWatcher::watch_file(fs::path path, WatchCallback on_change) {
    if (!initialized_.load(std::memory_order_acquire) || !on_change) return k_invalid_handle;

    Watch w;
    w.kind = WatchKind::File;
    w.target = std::move(path);
    w.callback = std::move(on_change);

    std::lock_guard<std::mutex> lk(watches_mutex_);
    w.handle = next_handle_++;
    auto handle = w.handle;
    watches_.push_back(std::move(w));
    return handle;
}

FileWatcher::WatchHandle FileWatcher::watch_directory(fs::path dir,
                                                     std::string extension_filter,
                                                     WatchCallback on_change) {
    if (!initialized_.load(std::memory_order_acquire) || !on_change) return k_invalid_handle;

    Watch w;
    w.kind = WatchKind::Directory;
    w.target = std::move(dir);
    w.ext_filter = std::move(extension_filter);
    w.callback = std::move(on_change);

    std::lock_guard<std::mutex> lk(watches_mutex_);
    w.handle = next_handle_++;
    auto handle = w.handle;
    watches_.push_back(std::move(w));
    return handle;
}

void FileWatcher::unwatch(WatchHandle h) {
    if (h == k_invalid_handle) return;
    std::lock_guard<std::mutex> lk(watches_mutex_);
    watches_.erase(std::remove_if(watches_.begin(), watches_.end(),
                                  [h](const Watch& w) { return w.handle == h; }),
                   watches_.end());
}

void FileWatcher::poll_main_thread() {
    std::vector<PendingEvent> drained;
    {
        std::lock_guard<std::mutex> lk(queue_mutex_);
        if (pending_.empty()) return;
        drained.swap(pending_);
    }

    // Snapshot callbacks under the watches lock, then invoke without it
    // to avoid deadlock if a callback registers/unregisters watches.
    std::vector<std::pair<WatchCallback, fs::path>> to_fire;
    {
        std::lock_guard<std::mutex> lk(watches_mutex_);
        to_fire.reserve(drained.size());
        for (const auto& ev : drained) {
            for (const auto& w : watches_) {
                if (w.handle == ev.handle) {
                    to_fire.emplace_back(w.callback, ev.path);
                    break;
                }
            }
        }
    }

    for (auto& [cb, path] : to_fire) {
        if (cb) cb(path);
    }
}

int64_t FileWatcher::to_nanos(fs::file_time_type t) noexcept {
    using namespace std::chrono;
    return duration_cast<nanoseconds>(t.time_since_epoch()).count();
}

int64_t FileWatcher::now_nanos() noexcept {
    using namespace std::chrono;
    return duration_cast<nanoseconds>(
               fs::file_time_type::clock::now().time_since_epoch())
        .count();
}

void FileWatcher::scan_watch(Watch& w, std::vector<PendingEvent>& out, int64_t now_ns) {
    std::error_code ec;

    auto record = [&](const fs::path& p) {
        auto mtime = fs::last_write_time(p, ec);
        if (ec) return;
        int64_t mt_ns = to_nanos(mtime);
        // Skip files actively being written; pick them up on a later tick.
        if (now_ns - mt_ns < k_settle_window_ns) return;

        std::string key = p.lexically_normal().string();
        auto it = w.mtimes.find(key);
        if (it == w.mtimes.end()) {
            w.mtimes.emplace(std::move(key), mt_ns);
            // First scan: prime cache without firing.
            if (!w.first_scan) {
                out.push_back(PendingEvent{ w.handle, p });
            }
        } else if (it->second != mt_ns) {
            it->second = mt_ns;
            out.push_back(PendingEvent{ w.handle, p });
        }
    };

    if (w.kind == WatchKind::File) {
        if (fs::exists(w.target, ec)) {
            record(w.target);
        }
    } else {
        if (!fs::exists(w.target, ec) || !fs::is_directory(w.target, ec)) {
            return;
        }
        for (auto it = fs::recursive_directory_iterator(w.target, ec);
             !ec && it != fs::recursive_directory_iterator{}; it.increment(ec)) {
            if (ec) break;
            const auto& entry = *it;
            if (!entry.is_regular_file(ec)) continue;
            if (!w.ext_filter.empty()) {
                auto ext = entry.path().extension().string();
                if (ext != w.ext_filter) continue;
            }
            record(entry.path());
        }
    }

    w.first_scan = false;
}

void FileWatcher::worker_loop() {
    while (!stop_.load(std::memory_order_acquire)) {
        std::vector<PendingEvent> events;
        int64_t now_ns = now_nanos();
        {
            std::lock_guard<std::mutex> lk(watches_mutex_);
            for (auto& w : watches_) {
                scan_watch(w, events, now_ns);
            }
        }
        if (!events.empty()) {
            std::lock_guard<std::mutex> lk(queue_mutex_);
            pending_.insert(pending_.end(),
                            std::make_move_iterator(events.begin()),
                            std::make_move_iterator(events.end()));
        }

        std::unique_lock<std::mutex> lk(wake_mutex_);
        wake_cv_.wait_for(lk, poll_interval_,
                          [this] { return stop_.load(std::memory_order_acquire); });
    }
}

} // namespace mmo::engine::core::asset
