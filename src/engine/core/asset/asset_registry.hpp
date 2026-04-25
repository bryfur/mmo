#pragma once

#include "engine/core/asset/file_watcher.hpp"
#include "engine/core/handle.hpp"
#include "engine/core/logger.hpp"

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace mmo::engine::core::asset {

/**
 * @brief Generic per-type asset registry with hot-reload support.
 *
 * `T` may be any type whose load/reload semantics can be expressed by a
 * caller-supplied LoadFn: `bool(const std::filesystem::path&, T&)`. By default
 * the registry calls `T::reload_from(path)` returning `bool` if defined;
 * callers can override this with `set_load_fn`.
 *
 * Handles are stable across reload (slot reused), invalidated on unload.
 */
template<typename T> class AssetRegistry {
public:
    using Handle = engine::core::Handle<T>;
    using LoadFn = std::function<bool(const std::filesystem::path&, T&)>;

    AssetRegistry() { load_fn_ = default_load_fn(); }

    void set_load_fn(LoadFn fn) { load_fn_ = std::move(fn); }

    Handle load(std::string name, std::filesystem::path path) {
        auto entry = std::make_unique<Entry>();
        entry->name = std::move(name);
        entry->path = std::move(path);
        if (!load_fn_(entry->path, entry->value)) {
            ENGINE_LOG_ERROR("hot_reload", "AssetRegistry: failed to load '{}'", entry->path.string());
            return Handle::invalid();
        }
        Handle h = next_handle();
        entry->handle = h;
        if (watcher_) {
            install_watch(*entry);
        }
        name_to_handle_[entry->name] = h;
        slots_[h.index] = std::move(entry);
        return h;
    }

    T* get(Handle h) {
        auto* entry = entry_at(h);
        return entry ? &entry->value : nullptr;
    }

    const T* get(Handle h) const {
        auto* entry = const_cast<AssetRegistry*>(this)->entry_at(h);
        return entry ? &entry->value : nullptr;
    }

    Handle find(const std::string& name) const {
        auto it = name_to_handle_.find(name);
        return it == name_to_handle_.end() ? Handle::invalid() : it->second;
    }

    bool reload(Handle h) {
        auto* entry = entry_at(h);
        if (!entry) {
            return false;
        }
        T fresh{};
        if (!load_fn_(entry->path, fresh)) {
            ENGINE_LOG_WARN("hot_reload", "AssetRegistry: reload failed for '{}'", entry->path.string());
            return false;
        }
        entry->value = std::move(fresh);
        ENGINE_LOG_INFO("hot_reload", "AssetRegistry: reloaded '{}'", entry->path.string());
        return true;
    }

    void unload(Handle h) {
        if (!h.is_valid() || h.index >= slots_.size()) {
            return;
        }
        auto& slot = slots_[h.index];
        if (!slot || slot->handle.generation != h.generation) {
            return;
        }
        if (slot->watch != FileWatcher::k_invalid_handle && watcher_) {
            watcher_->unwatch(slot->watch);
        }
        name_to_handle_.erase(slot->name);
        // Bump generation for the slot so old handles fail validation.
        free_generations_.push_back(slot->handle.generation + 1);
        free_indices_.push_back(h.index);
        slot.reset();
    }

    void enable_hot_reload(FileWatcher& watcher) {
        watcher_ = &watcher;
        for (auto& slot : slots_) {
            if (slot && slot->watch == FileWatcher::k_invalid_handle) {
                install_watch(*slot);
            }
        }
    }

private:
    struct Entry {
        std::string name;
        std::filesystem::path path;
        T value{};
        Handle handle{};
        FileWatcher::WatchHandle watch = FileWatcher::k_invalid_handle;
    };

    static LoadFn default_load_fn() {
        return [](const std::filesystem::path& p, T& out) -> bool { return out.reload_from(p); };
    }

    Handle next_handle() {
        if (!free_indices_.empty()) {
            uint32_t idx = free_indices_.back();
            free_indices_.pop_back();
            uint32_t gen = free_generations_.back();
            free_generations_.pop_back();
            return Handle{idx, gen};
        }
        uint32_t idx = static_cast<uint32_t>(slots_.size());
        slots_.emplace_back();
        return Handle{idx, 1};
    }

    Entry* entry_at(Handle h) {
        if (!h.is_valid() || h.index >= slots_.size()) {
            return nullptr;
        }
        auto& slot = slots_[h.index];
        if (!slot) {
            return nullptr;
        }
        if (slot->handle.generation != h.generation) {
            return nullptr;
        }
        return slot.get();
    }

    void install_watch(Entry& entry) {
        Handle h = entry.handle;
        entry.watch = watcher_->watch_file(entry.path, [this, h](const std::filesystem::path&) { reload(h); });
    }

    LoadFn load_fn_;
    FileWatcher* watcher_ = nullptr;
    std::vector<std::unique_ptr<Entry>> slots_;
    std::vector<uint32_t> free_indices_;
    std::vector<uint32_t> free_generations_;
    std::unordered_map<std::string, Handle> name_to_handle_;
};

} // namespace mmo::engine::core::asset
