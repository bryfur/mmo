#pragma once

#include "engine/gpu/gpu_device.hpp"
#include "engine/gpu/gpu_texture.hpp"
#include "engine/gpu/gpu_uniforms.hpp"
#include "engine/gpu/pipeline_registry.hpp"
#include <glm/glm.hpp>
#include <memory>

namespace mmo::engine::render {

namespace gpu = mmo::engine::gpu;

/**
 * Ambient Occlusion manager (supports SSAO and GTAO).
 *
 * When enabled, the scene is rendered to an offscreen color+depth target.
 * AO is computed from the depth buffer at half resolution, bilaterally blurred,
 * then composited with the scene color onto the swapchain.
 */
class AmbientOcclusion {
public:
    AmbientOcclusion();
    ~AmbientOcclusion();

    AmbientOcclusion(const AmbientOcclusion&) = delete;
    AmbientOcclusion& operator=(const AmbientOcclusion&) = delete;

    bool init(gpu::GPUDevice& device, int width, int height);
    void shutdown();
    void resize(int width, int height);

    // Offscreen pass (replaces begin_main_pass when AO is on)
    SDL_GPURenderPass* begin_offscreen_pass(SDL_GPUCommandBuffer* cmd);
    void end_offscreen_pass();

    // Post-processing passes
    void render_ssao_pass(SDL_GPUCommandBuffer* cmd, gpu::PipelineRegistry& pipelines,
                          const glm::mat4& projection, const glm::mat4& inv_projection);
    void render_gtao_pass(SDL_GPUCommandBuffer* cmd, gpu::PipelineRegistry& pipelines,
                          const glm::mat4& projection, const glm::mat4& inv_projection);
    void render_blur_pass(SDL_GPUCommandBuffer* cmd, gpu::PipelineRegistry& pipelines);
    void render_composite_pass(SDL_GPUCommandBuffer* cmd, gpu::PipelineRegistry& pipelines,
                               SDL_GPUTexture* swapchain_target);

    bool is_ready() const;

    // Offscreen depth for shadow/other systems that need the depth texture
    gpu::GPUTexture* offscreen_depth() { return offscreen_depth_.get(); }

private:
    void create_textures(int width, int height);
    void create_samplers();

    gpu::GPUDevice* device_ = nullptr;

    // Full-resolution offscreen targets
    std::unique_ptr<gpu::GPUTexture> offscreen_color_;
    std::unique_ptr<gpu::GPUTexture> offscreen_depth_;

    // Half-resolution AO textures (ping-pong for blur)
    std::unique_ptr<gpu::GPUTexture> ao_texture_;
    std::unique_ptr<gpu::GPUTexture> ao_blurred_;

    SDL_GPUSampler* nearest_clamp_sampler_ = nullptr;
    SDL_GPUSampler* linear_clamp_sampler_ = nullptr;

    SDL_GPURenderPass* current_pass_ = nullptr;

    int width_ = 0;
    int height_ = 0;
    int ao_width_ = 0;
    int ao_height_ = 0;
};

} // namespace mmo::engine::render
