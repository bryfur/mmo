#include "engine/memory/arena.hpp"

#include <algorithm>
#include <cstdlib>
#include <new>

namespace mmo::engine::memory {

namespace {

inline std::size_t align_up(std::size_t v, std::size_t a) {
    return (v + (a - 1)) & ~(a - 1);
}

} // namespace

Arena::Arena(std::size_t initial_chunk_size)
    : default_chunk_size_(initial_chunk_size > 0 ? initial_chunk_size : k_default_chunk_size) {}

Arena::Arena(void* buffer, std::size_t size) {
    Chunk c;
    c.base = static_cast<std::byte*>(buffer);
    c.size = size;
    c.used = 0;
    chunks_.push_back(c);
    owns_memory_ = false;
    fixed_buffer_ = true;
}

Arena::~Arena() {
    destroy_all();
    free_chunks();
}

Arena::Arena(Arena&& other) noexcept
    : chunks_(std::move(other.chunks_)),
      destructors_(std::move(other.destructors_)),
      default_chunk_size_(other.default_chunk_size_),
      owns_memory_(other.owns_memory_),
      fixed_buffer_(other.fixed_buffer_) {
    other.chunks_.clear();
    other.destructors_.clear();
    other.owns_memory_ = true;
    other.fixed_buffer_ = false;
}

Arena& Arena::operator=(Arena&& other) noexcept {
    if (this != &other) {
        destroy_all();
        free_chunks();
        chunks_ = std::move(other.chunks_);
        destructors_ = std::move(other.destructors_);
        default_chunk_size_ = other.default_chunk_size_;
        owns_memory_ = other.owns_memory_;
        fixed_buffer_ = other.fixed_buffer_;
        other.chunks_.clear();
        other.destructors_.clear();
        other.owns_memory_ = true;
        other.fixed_buffer_ = false;
    }
    return *this;
}

void* Arena::allocate(std::size_t size, std::size_t align) {
    if (size == 0) {
        return nullptr;
    }
    if (align == 0) {
        align = alignof(std::max_align_t);
    }

    if (!chunks_.empty()) {
        Chunk& cur = chunks_.back();
        std::size_t base_addr = reinterpret_cast<std::size_t>(cur.base);
        std::size_t aligned = align_up(base_addr + cur.used, align) - base_addr;
        if (aligned + size <= cur.size) {
            void* p = cur.base + aligned;
            cur.used = aligned + size;
            return p;
        }
    }

    if (fixed_buffer_) {
        return nullptr;
    }
    if (!grow(size + align)) {
        return nullptr;
    }

    Chunk& cur = chunks_.back();
    std::size_t base_addr = reinterpret_cast<std::size_t>(cur.base);
    std::size_t aligned = align_up(base_addr + cur.used, align) - base_addr;
    if (aligned + size > cur.size) {
        return nullptr;
    }
    void* p = cur.base + aligned;
    cur.used = aligned + size;
    return p;
}

bool Arena::grow(std::size_t min_bytes) {
    std::size_t chunk_size = std::max(default_chunk_size_, min_bytes);
    void* mem = std::malloc(chunk_size);
    if (!mem) {
        return false;
    }
    Chunk c;
    c.base = static_cast<std::byte*>(mem);
    c.size = chunk_size;
    c.used = 0;
    chunks_.push_back(c);
    return true;
}

void Arena::reset() {
    destroy_all();
    if (fixed_buffer_) {
        if (!chunks_.empty()) {
            chunks_[0].used = 0;
        }
        return;
    }
    // Keep the first chunk to amortize allocations across resets; free the rest.
    if (chunks_.size() > 1) {
        for (std::size_t i = 1; i < chunks_.size(); ++i) {
            std::free(chunks_[i].base);
        }
        chunks_.resize(1);
    }
    if (!chunks_.empty()) {
        chunks_[0].used = 0;
    }
}

std::size_t Arena::bytes_used() const noexcept {
    std::size_t total = 0;
    for (const auto& c : chunks_) total += c.used;
    return total;
}

std::size_t Arena::bytes_capacity() const noexcept {
    std::size_t total = 0;
    for (const auto& c : chunks_) total += c.size;
    return total;
}

void Arena::register_destructor(void* obj, void (*fn)(void*)) {
    destructors_.push_back(Destructor{obj, fn});
}

void Arena::destroy_all() noexcept {
    for (auto it = destructors_.rbegin(); it != destructors_.rend(); ++it) {
        it->fn(it->obj);
    }
    destructors_.clear();
}

void Arena::free_chunks() noexcept {
    if (owns_memory_) {
        for (auto& c : chunks_) {
            std::free(c.base);
        }
    }
    chunks_.clear();
}

} // namespace mmo::engine::memory
