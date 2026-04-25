#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace mmo::engine::core::asset {

using WatchCallback = std::function<void(const std::filesystem::path&)>;

/**
 * @brief Polling-based, cross-platform file/directory mtime watcher.
 *
 * A background jthread polls registered paths every poll_interval. Detected
 * changes are pushed onto an mpsc queue; callbacks are dispatched on the
 * main thread by poll_main_thread() so they may safely touch GPU state.
 *
 * Watches that target a file refresh that file only. Watches that target a
 * directory recursively scan for files matching extension_filter (".ext").
 * An empty filter matches every regular file.
 *
 * Thread-safety: init, shutdown, watch_file, watch_directory, and unwatch
 * are callable from the main thread only. poll_main_thread() must run on
 * the main thread.
 */
class FileWatcher {
public:
    using WatchHandle = uint32_t;
    static constexpr WatchHandle k_invalid_handle = 0;

    FileWatcher() = default;
    ~FileWatcher();

    FileWatcher(const FileWatcher&) = delete;
    FileWatcher& operator=(const FileWatcher&) = delete;
    FileWatcher(FileWatcher&&) = delete;
    FileWatcher& operator=(FileWatcher&&) = delete;

    static FileWatcher& instance();

    bool init(std::chrono::milliseconds poll_interval = std::chrono::milliseconds{1000});
    void shutdown();

    bool is_initialized() const noexcept { return initialized_; }

    WatchHandle watch_file(std::filesystem::path path, WatchCallback on_change);
    WatchHandle watch_directory(std::filesystem::path dir, std::string extension_filter, WatchCallback on_change);
    void unwatch(WatchHandle h);

    // Drain pending changes, fire callbacks on the calling (main) thread.
    void poll_main_thread();

private:
    enum class WatchKind : uint8_t { File, Directory };

    struct Watch {
        WatchHandle handle = k_invalid_handle;
        WatchKind kind = WatchKind::File;
        std::filesystem::path target;
        std::string ext_filter; // ".spv", "" = any
        WatchCallback callback;
        // Per-path mtime cache (file_time_type stored as nanoseconds since epoch).
        std::unordered_map<std::string, int64_t> mtimes;
        bool first_scan = true;
    };

    struct PendingEvent {
        WatchHandle handle = k_invalid_handle;
        std::filesystem::path path;
    };

    void worker_loop();
    void scan_watch(Watch& w, std::vector<PendingEvent>& out, int64_t now_ns);

    static int64_t to_nanos(std::filesystem::file_time_type t) noexcept;
    static int64_t now_nanos() noexcept;

    std::atomic<bool> initialized_{false};
    std::atomic<bool> stop_{false};
    std::chrono::milliseconds poll_interval_{1000};

    std::mutex watches_mutex_;
    std::vector<Watch> watches_;
    WatchHandle next_handle_ = 1;

    std::mutex queue_mutex_;
    std::vector<PendingEvent> pending_;

    std::condition_variable wake_cv_;
    std::mutex wake_mutex_;

    std::jthread worker_;
};

} // namespace mmo::engine::core::asset
