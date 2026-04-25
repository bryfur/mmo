#include "engine/render_graph/transient_allocator.hpp"
#include "engine/core/logger.hpp"
#include "engine/gpu/gpu_device.hpp"

#include <functional>

namespace mmo::engine::render_graph {

size_t TransientAllocator::DescKeyHash::operator()(const DescKey& k) const noexcept {
    size_t h = 0;
    auto mix = [&](uint64_t v) { h ^= std::hash<uint64_t>{}(v) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2); };
    mix(k.desc.width);
    mix(k.desc.height);
    mix(static_cast<uint64_t>(k.desc.format));
    mix(static_cast<uint64_t>(k.desc.usage));
    mix(k.desc.mip_levels);
    mix(k.desc.array_layers);
    mix(k.desc.sample_count);
    return h;
}

TransientAllocator::~TransientAllocator() {
    shutdown();
}

bool TransientAllocator::init(gpu::GPUDevice& device) {
    device_ = &device;
    return true;
}

void TransientAllocator::shutdown() {
    evict_all();
    device_ = nullptr;
}

SDL_GPUTexture* TransientAllocator::acquire_texture(const TextureDesc& desc, const char* debug_name) {
    ++acquires_;
    DescKey key{desc};
    auto& bucket = pool_[key];
    for (auto& e : bucket) {
        if (!e.in_use) {
            e.in_use = true;
            ++reuses_;
            return e.tex;
        }
    }
    if (!device_) {
        // Test mode: hand out a sentinel pointer keyed by bucket size so reuse logic still works.
        // Reinterpret a stable per-bucket index to a fake non-null pointer.
        auto* fake = reinterpret_cast<SDL_GPUTexture*>(reinterpret_cast<uintptr_t>(&bucket) + bucket.size() + 1);
        bucket.push_back(Entry{fake, true});
        return fake;
    }
    SDL_GPUTextureCreateInfo info{};
    info.type = (desc.array_layers > 1) ? SDL_GPU_TEXTURETYPE_2D_ARRAY : SDL_GPU_TEXTURETYPE_2D;
    info.format = desc.format;
    info.usage = desc.usage;
    info.width = desc.width;
    info.height = desc.height;
    info.layer_count_or_depth = desc.array_layers;
    info.num_levels = desc.mip_levels;
    SDL_GPUSampleCount samples = SDL_GPU_SAMPLECOUNT_1;
    switch (desc.sample_count) {
        case 2:
            samples = SDL_GPU_SAMPLECOUNT_2;
            break;
        case 4:
            samples = SDL_GPU_SAMPLECOUNT_4;
            break;
        case 8:
            samples = SDL_GPU_SAMPLECOUNT_8;
            break;
        default:
            samples = SDL_GPU_SAMPLECOUNT_1;
            break;
    }
    info.sample_count = samples;
    info.props = 0;

    SDL_GPUTexture* tex = SDL_CreateGPUTexture(device_->handle(), &info);
    if (!tex) {
        ENGINE_LOG_ERROR("rg", "TransientAllocator: SDL_CreateGPUTexture failed for '{}'",
                         debug_name ? debug_name : "(unnamed)");
        return nullptr;
    }
    if (debug_name) {
        SDL_SetGPUTextureName(device_->handle(), tex, debug_name);
    }
    bucket.push_back(Entry{tex, true});
    return tex;
}

void TransientAllocator::release_texture(const TextureDesc& desc, SDL_GPUTexture* tex) {
    if (!tex) {
        return;
    }
    DescKey key{desc};
    auto it = pool_.find(key);
    if (it == pool_.end()) {
        return;
    }
    for (auto& e : it->second) {
        if (e.tex == tex) {
            e.in_use = false;
            return;
        }
    }
}

uint32_t TransientAllocator::pool_size() const noexcept {
    uint32_t n = 0;
    for (const auto& [k, v] : pool_) n += static_cast<uint32_t>(v.size());
    return n;
}

void TransientAllocator::release_all() {
    for (auto& [k, bucket] : pool_) {
        for (auto& e : bucket) e.in_use = false;
        (void)k;
    }
}

void TransientAllocator::evict_all() {
    if (!device_) {
        pool_.clear();
        return;
    }
    for (auto& [k, bucket] : pool_) {
        for (auto& e : bucket) {
            if (e.tex) {
                SDL_ReleaseGPUTexture(device_->handle(), e.tex);
            }
        }
        (void)k;
    }
    pool_.clear();
}

} // namespace mmo::engine::render_graph
