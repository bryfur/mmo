#pragma once

#include "engine/core/jobs/job_system.hpp"
#include "engine/core/jobs/task_handle.hpp"

#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>

namespace mmo::engine::core::jobs {

// Below this total, dispatch overhead exceeds the work — run serially on caller.
// Tuned conservatively: animation/cull loops with a few dozen entities should
// not pay the job-system tax. Callers that know the work is heavy can pass an
// explicit `grain` to bypass this.
inline constexpr std::size_t PARALLEL_MIN_TOTAL = 256;

// Fn signature: void(size_t chunk_begin, size_t chunk_end).
// Chunk form is used (not per-index) so tight loops avoid per-element call overhead.
template<typename Fn> void parallel_for(std::size_t begin, std::size_t end, Fn&& fn, std::size_t grain = 0) {
    if (end <= begin) {
        return;
    }
    const std::size_t total = end - begin;

    auto& js = JobSystem::instance();
    const unsigned workers = js.is_initialized() ? js.worker_count() : 1u;

    if (!js.is_initialized() || workers <= 1) {
        fn(begin, end);
        return;
    }

    // Auto-grain mode applies the small-workload guard. Explicit grain bypasses
    // it (caller is opting in to dispatch overhead).
    if (grain == 0) {
        if (total < PARALLEL_MIN_TOTAL) {
            fn(begin, end);
            return;
        }
        grain = std::max<std::size_t>(64, total / (static_cast<std::size_t>(workers) * 4));
    }

    if (total <= grain) {
        fn(begin, end);
        return;
    }

    std::vector<TaskHandle> handles;
    handles.reserve((total + grain - 1) / grain);
    for (std::size_t i = begin; i < end; i += grain) {
        const std::size_t chunk_end = std::min(end, i + grain);
        handles.push_back(js.submit([i, chunk_end, fn]() { fn(i, chunk_end); }));
    }
    for (auto& h : handles) {
        js.wait(h);
    }
}

} // namespace mmo::engine::core::jobs
