#pragma once

#include "engine/render_graph/render_graph_resource.hpp"
#include <cstdint>
#include <SDL3/SDL_gpu.h>
#include <unordered_map>
#include <vector>

namespace mmo::engine::gpu {
class GPUDevice;
}

namespace mmo::engine::render_graph {

// Pools transient SDL_GPUTextures keyed by their TextureDesc.
// On acquire(), pops a free texture matching desc, or creates a new one.
// On release(), pushes back into the pool. release_all() returns everything.
// Underlying SDL textures persist across frames until evict_all() (e.g. on resize).
class TransientAllocator {
public:
    TransientAllocator() = default;
    ~TransientAllocator();

    TransientAllocator(const TransientAllocator&) = delete;
    TransientAllocator& operator=(const TransientAllocator&) = delete;

    bool init(gpu::GPUDevice& device);
    void shutdown();

    // Acquire a texture matching desc. If pool has a free one, reuses it.
    SDL_GPUTexture* acquire_texture(const TextureDesc& desc, const char* debug_name);

    // Return a texture to the pool for reuse on next acquire.
    void release_texture(const TextureDesc& desc, SDL_GPUTexture* tex);

    // Stats for telemetry.
    uint32_t acquires_this_frame() const noexcept { return acquires_; }
    uint32_t reuses_this_frame() const noexcept { return reuses_; }
    uint32_t pool_size() const noexcept;

    void begin_frame() noexcept {
        acquires_ = 0;
        reuses_ = 0;
    }

    // Release all textures back into pool keyed by desc (called at end_frame()).
    void release_all();

    // Destroy every pooled texture (called on resize / shutdown).
    void evict_all();

private:
    struct DescKey {
        TextureDesc desc;
        bool operator==(const DescKey& o) const noexcept { return desc == o.desc; }
    };
    struct DescKeyHash {
        size_t operator()(const DescKey& k) const noexcept;
    };

    struct Entry {
        SDL_GPUTexture* tex = nullptr;
        bool in_use = false;
    };

    gpu::GPUDevice* device_ = nullptr;
    std::unordered_map<DescKey, std::vector<Entry>, DescKeyHash> pool_;

    uint32_t acquires_ = 0;
    uint32_t reuses_ = 0;
};

} // namespace mmo::engine::render_graph
