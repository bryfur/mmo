#pragma once

#include "engine/gpu/gpu_device.hpp"
#include "engine/gpu/gpu_texture.hpp"
#include "engine/gpu/gpu_uniforms.hpp"
#include "engine/gpu/pipeline_registry.hpp"
#include <memory>

namespace mmo::engine::render {

namespace gpu = mmo::engine::gpu;

/**
 * Bloom post-processing effect.
 *
 * When enabled (alongside AO offscreen rendering), extracts bright pixels
 * from the scene color buffer, progressively downsamples through a mip chain,
 * then upsamples back to produce a soft glow. The final bloom texture is
 * composited with the scene color in the composite pass.
 *
 * Uses a 13-tap downsample filter and 9-tap tent upsample (Jimenez 2014).
 */
class Bloom {
public:
    Bloom();
    ~Bloom();

    Bloom(const Bloom&) = delete;
    Bloom& operator=(const Bloom&) = delete;

    bool init(gpu::GPUDevice& device, int width, int height);
    void shutdown();
    void resize(int width, int height);

    bool is_ready() const;

    /**
     * Run the bloom downsample + upsample chain.
     * Must be called after the offscreen scene render but before the composite pass.
     *
     * @param cmd         Active command buffer (no render pass should be active)
     * @param registry    Pipeline registry for bloom pipelines
     * @param scene_color The offscreen scene color texture to extract bloom from
     */
    void render(SDL_GPUCommandBuffer* cmd, gpu::PipelineRegistry& registry, SDL_GPUTexture* scene_color,
                float threshold = 1.0f);

    /**
     * Get the final bloom result texture for the composite pass.
     * Returns the first (largest) mip in the chain after upsampling.
     */
    SDL_GPUTexture* bloom_texture() const;

private:
    void create_textures(int width, int height);
    void create_sampler();

    gpu::GPUDevice* device_ = nullptr;

    // Downsample/upsample mip chain. 4 mips produces visually equivalent bloom
    // for typical scene resolutions while saving 1 downsample + 1 upsample pass
    // compared to 5 mips (~2 fullscreen-ish passes).
    static constexpr int MIP_COUNT = 4;
    std::unique_ptr<gpu::GPUTexture> mip_textures_[MIP_COUNT];

    SDL_GPUSampler* linear_sampler_ = nullptr;

    int width_ = 0;
    int height_ = 0;
};

} // namespace mmo::engine::render
