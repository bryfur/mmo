#pragma once

#include "engine/memory/arena.hpp"

#include <cstddef>

namespace mmo::engine::memory {

class FrameAllocator {
public:
    FrameAllocator() = default;
    explicit FrameAllocator(std::size_t initial_chunk_size)
        : arenas_{Arena(initial_chunk_size), Arena(initial_chunk_size)} {}

    FrameAllocator(const FrameAllocator&) = delete;
    FrameAllocator& operator=(const FrameAllocator&) = delete;

    FrameAllocator(FrameAllocator&&) = default;
    FrameAllocator& operator=(FrameAllocator&&) = default;

    Arena& this_frame() noexcept { return arenas_[current_]; }
    Arena& last_frame() noexcept { return arenas_[1 - current_]; }
    const Arena& this_frame() const noexcept { return arenas_[current_]; }
    const Arena& last_frame() const noexcept { return arenas_[1 - current_]; }

    // Flip the active arena and reset the new "this frame" so old contents are recycled.
    void swap() {
        current_ = 1 - current_;
        arenas_[current_].reset();
    }

private:
    Arena arenas_[2]{};
    int current_ = 0;
};

} // namespace mmo::engine::memory
