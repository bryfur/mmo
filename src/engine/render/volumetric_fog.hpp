#pragma once

#include "engine/gpu/gpu_device.hpp"
#include "engine/gpu/gpu_texture.hpp"
#include "engine/gpu/gpu_uniforms.hpp"
#include "engine/gpu/pipeline_registry.hpp"
#include "engine/render/shadow_map.hpp"
#include "engine/scene/camera_state.hpp"
#include <glm/glm.hpp>
#include <memory>

namespace mmo::engine::render {

namespace gpu = mmo::engine::gpu;

/**
 * Volumetric fog with god rays (light shafts) post-processing effect.
 *
 * Renders at half resolution for performance. Ray-marches through a
 * height-based fog volume, sampling the shadow map to produce
 * inscattered light (god rays) where sunlight penetrates fog.
 *
 * Output is an RGBA16F texture:
 *   RGB = inscattered fog light
 *   A   = fog opacity (1 - transmittance)
 *
 * Composited in the final composite pass via premultiplied alpha blend.
 */
class VolumetricFog {
public:
    VolumetricFog();
    ~VolumetricFog();

    VolumetricFog(const VolumetricFog&) = delete;
    VolumetricFog& operator=(const VolumetricFog&) = delete;

    bool init(gpu::GPUDevice& device, int width, int height);
    void shutdown();
    void resize(int width, int height);

    bool is_ready() const;

    /**
     * Run the volumetric fog ray march pass.
     * Must be called after the offscreen scene render and shadow passes,
     * but before the composite pass. No render pass should be active.
     *
     * @param cmd         Active command buffer
     * @param registry    Pipeline registry
     * @param depth_texture  Offscreen scene depth texture
     * @param shadow_map  Shadow map manager (uses cascade 0)
     * @param camera      Camera state for the frame
     * @param light_dir   Directional light direction (normalized)
     */
    void render(SDL_GPUCommandBuffer* cmd, gpu::PipelineRegistry& registry,
                SDL_GPUTexture* depth_texture,
                const ShadowMap& shadow_map,
                const scene::CameraState& camera,
                const glm::vec3& light_dir,
                bool god_rays_enabled = true,
                bool fog_enabled = true,
                float density_multiplier = 1.0f);

    /**
     * Get the fog result texture for the composite pass.
     */
    SDL_GPUTexture* fog_texture() const;

private:
    void create_textures(int width, int height);
    void create_sampler();

    gpu::GPUDevice* device_ = nullptr;
    std::unique_ptr<gpu::GPUTexture> fog_texture_;  // Half-res RGBA16F
    SDL_GPUSampler* linear_sampler_ = nullptr;

    int width_ = 0;
    int height_ = 0;
    int fog_width_ = 0;
    int fog_height_ = 0;

    // Cache view_projection inverse: skips ~80 flops on frames where the camera is idle.
    glm::mat4 cached_view_projection_{0.0f};
    glm::mat4 cached_inv_view_projection_{1.0f};
};

} // namespace mmo::engine::render
