#include "engine/gpu/gpu_debug.hpp"
#include "engine/gpu/gpu_device.hpp"

namespace mmo::engine::gpu {

GPUDebugGroup::GPUDebugGroup(SDL_GPUCommandBuffer* cb, const char* name) noexcept
    : cb_(cb) {
    if (cb_ && name) {
        SDL_PushGPUDebugGroup(cb_, name);
    } else {
        cb_ = nullptr;
    }
}

GPUDebugGroup::~GPUDebugGroup() noexcept {
    if (cb_) {
        SDL_PopGPUDebugGroup(cb_);
    }
}

bool GPUTimestampPool::init(GPUDevice& /*device*/, size_t pair_count) {
    capacity_ = pair_count;
    pass_names_.reserve(pair_count);
    supported_ = false;
    return true;
}

void GPUTimestampPool::shutdown() {
    pass_names_.clear();
    capacity_ = 0;
    supported_ = false;
}

int GPUTimestampPool::reserve_pair(const char* pass_name) {
    if (!pass_name) return -1;
    if (pass_names_.size() >= capacity_) return -1;
    const int idx = static_cast<int>(pass_names_.size());
    pass_names_.emplace_back(pass_name);
    return idx;
}

void GPUTimestampPool::write_begin(SDL_GPUCommandBuffer* /*cb*/, int /*pair_idx*/) {
    // No-op until SDL3 exposes GPU timestamp queries.
}

void GPUTimestampPool::write_end(SDL_GPUCommandBuffer* /*cb*/, int /*pair_idx*/) {
    // No-op until SDL3 exposes GPU timestamp queries.
}

void GPUTimestampPool::read_results(std::vector<std::pair<std::string, float>>& ms_out) {
    ms_out.clear();
    ms_out.reserve(pass_names_.size());
    for (const auto& name : pass_names_) {
        ms_out.emplace_back(name, 0.0f);
    }
}

void GPUTimestampPool::begin_frame() {
    pass_names_.clear();
}

void GPUTimestampPool::end_frame(SDL_GPUCommandBuffer* /*cb*/) {
    // No resolve step until SDL3 exposes timestamp queries.
}

GPUTimerScope::GPUTimerScope(GPUTimestampPool& pool, SDL_GPUCommandBuffer* cb, const char* pass_name) noexcept
    : pool_(&pool), cb_(cb) {
    pair_idx_ = pool_->reserve_pair(pass_name);
    if (pair_idx_ >= 0) {
        pool_->write_begin(cb_, pair_idx_);
    }
}

GPUTimerScope::~GPUTimerScope() noexcept {
    if (pool_ && pair_idx_ >= 0) {
        pool_->write_end(cb_, pair_idx_);
    }
}

} // namespace mmo::engine::gpu
