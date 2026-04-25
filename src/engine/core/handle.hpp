#pragma once

#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace mmo::engine::core {

template<typename Tag> struct Handle {
    uint32_t index = 0;
    uint32_t generation = 0;

    static constexpr uint32_t k_invalid_index = static_cast<uint32_t>(-1);

    static constexpr Handle invalid() { return Handle{k_invalid_index, 0}; }

    bool is_valid() const noexcept { return index != k_invalid_index; }

    friend bool operator==(const Handle& a, const Handle& b) noexcept {
        return a.index == b.index && a.generation == b.generation;
    }
    friend bool operator!=(const Handle& a, const Handle& b) noexcept { return !(a == b); }
};

template<typename Tag, typename T> class HandlePool {
public:
    using HandleT = Handle<Tag>;

    HandleT acquire(T value) {
        if (free_head_ != HandleT::k_invalid_index) {
            uint32_t idx = free_head_;
            Slot& slot = slots_[idx];
            free_head_ = slot.next_free;
            slot.next_free = k_occupied;
            slot.value = std::move(value);
            return HandleT{idx, slot.generation};
        }
        uint32_t idx = static_cast<uint32_t>(slots_.size());
        Slot s;
        s.generation = 1;
        s.next_free = k_occupied;
        s.value = std::move(value);
        slots_.push_back(std::move(s));
        return HandleT{idx, slots_[idx].generation};
    }

    bool release(HandleT h) {
        if (!h.is_valid() || h.index >= slots_.size()) {
            return false;
        }
        Slot& slot = slots_[h.index];
        if (slot.next_free != k_occupied) {
            return false;
        }
        if (slot.generation != h.generation) {
            return false;
        }
        slot.value = T{};
        slot.generation += 1;
        slot.next_free = free_head_;
        free_head_ = h.index;
        return true;
    }

    T* get(HandleT h) {
        if (!h.is_valid() || h.index >= slots_.size()) {
            return nullptr;
        }
        Slot& slot = slots_[h.index];
        if (slot.next_free != k_occupied) {
            return nullptr;
        }
        if (slot.generation != h.generation) {
            return nullptr;
        }
        return &slot.value;
    }

    const T* get(HandleT h) const {
        if (!h.is_valid() || h.index >= slots_.size()) {
            return nullptr;
        }
        const Slot& slot = slots_[h.index];
        if (slot.next_free != k_occupied) {
            return nullptr;
        }
        if (slot.generation != h.generation) {
            return nullptr;
        }
        return &slot.value;
    }

    bool contains(HandleT h) const { return get(h) != nullptr; }

    size_t size() const noexcept {
        size_t free_count = 0;
        for (uint32_t cur = free_head_; cur != HandleT::k_invalid_index;) {
            ++free_count;
            cur = slots_[cur].next_free;
        }
        return slots_.size() - free_count;
    }

    size_t capacity() const noexcept { return slots_.size(); }

    void clear() {
        slots_.clear();
        free_head_ = HandleT::k_invalid_index;
    }

private:
    // next_free == k_occupied means slot is live; otherwise it's an index into the free list.
    static constexpr uint32_t k_occupied = static_cast<uint32_t>(-2);

    struct Slot {
        T value{};
        uint32_t generation = 0;
        uint32_t next_free = k_occupied;
    };

    std::vector<Slot> slots_;
    uint32_t free_head_ = HandleT::k_invalid_index;
};

} // namespace mmo::engine::core

namespace std {
template<typename Tag> struct hash<::mmo::engine::core::Handle<Tag>> {
    size_t operator()(const ::mmo::engine::core::Handle<Tag>& h) const noexcept {
        return (static_cast<size_t>(h.generation) << 32) ^ static_cast<size_t>(h.index);
    }
};
} // namespace std
