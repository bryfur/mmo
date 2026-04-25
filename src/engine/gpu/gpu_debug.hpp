#pragma once

#include <cstddef>
#include <SDL3/SDL_gpu.h>
#include <string>
#include <utility>
#include <vector>

namespace mmo::engine::gpu {

class GPUDevice;

class GPUDebugGroup {
public:
    GPUDebugGroup(SDL_GPUCommandBuffer* cb, const char* name) noexcept;
    ~GPUDebugGroup() noexcept;
    GPUDebugGroup(const GPUDebugGroup&) = delete;
    GPUDebugGroup& operator=(const GPUDebugGroup&) = delete;
    GPUDebugGroup(GPUDebugGroup&&) = delete;
    GPUDebugGroup& operator=(GPUDebugGroup&&) = delete;

private:
    SDL_GPUCommandBuffer* cb_ = nullptr;
};

inline void insert_debug_label(SDL_GPUCommandBuffer* cb, const char* name) noexcept {
    if (cb && name) {
        SDL_InsertGPUDebugLabel(cb, name);
    }
}

// SDL3 GPU does not yet expose per-pass timestamp queries (as of SDL 3.2).
// This pool preserves the intended API so callers stay identical once SDL adds support;
// read_results currently returns zeroed durations but correct pass names.
class GPUTimestampPool {
public:
    bool init(GPUDevice& device, size_t pair_count);
    void shutdown();

    int reserve_pair(const char* pass_name);
    void write_begin(SDL_GPUCommandBuffer* cb, int pair_idx);
    void write_end(SDL_GPUCommandBuffer* cb, int pair_idx);

    void read_results(std::vector<std::pair<std::string, float>>& ms_out);

    void begin_frame();
    void end_frame(SDL_GPUCommandBuffer* cb);

    bool is_supported() const noexcept { return supported_; }

private:
    bool supported_ = false;
    size_t capacity_ = 0;
    std::vector<std::string> pass_names_;
};

class GPUTimerScope {
public:
    GPUTimerScope(GPUTimestampPool& pool, SDL_GPUCommandBuffer* cb, const char* pass_name) noexcept;
    ~GPUTimerScope() noexcept;
    GPUTimerScope(const GPUTimerScope&) = delete;
    GPUTimerScope& operator=(const GPUTimerScope&) = delete;

private:
    GPUTimestampPool* pool_ = nullptr;
    SDL_GPUCommandBuffer* cb_ = nullptr;
    int pair_idx_ = -1;
};

} // namespace mmo::engine::gpu

#define ENGINE_GPU_DBG_CAT2(a, b) a##b
#define ENGINE_GPU_DBG_CAT(a, b) ENGINE_GPU_DBG_CAT2(a, b)

#define ENGINE_GPU_SCOPE(cb, name) \
    ::mmo::engine::gpu::GPUDebugGroup ENGINE_GPU_DBG_CAT(_gpu_dbg_, __LINE__)((cb), (name))

#define ENGINE_GPU_LABEL(cb, name) ::mmo::engine::gpu::insert_debug_label((cb), (name))

#define ENGINE_GPU_TIMER(pool, cb, name) \
    ::mmo::engine::gpu::GPUTimerScope ENGINE_GPU_DBG_CAT(_gpu_tm_, __LINE__)((pool), (cb), (name))
