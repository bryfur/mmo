#include "ambient_occlusion.hpp"
#include "engine/gpu/gpu_pipeline.hpp"
#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_log.h>
#include <glm/gtc/matrix_inverse.hpp>

namespace mmo::engine::render {

AmbientOcclusion::AmbientOcclusion() = default;

AmbientOcclusion::~AmbientOcclusion() {
    shutdown();
}

bool AmbientOcclusion::init(gpu::GPUDevice& device, int width, int height) {
    device_ = &device;
    width_ = width;
    height_ = height;
    ao_width_ = std::max(1, width / 2);
    ao_height_ = std::max(1, height / 2);

    create_textures(width, height);
    create_samplers();

    if (!is_ready()) {
        SDL_Log("AmbientOcclusion: Failed to initialize");
        return false;
    }

    SDL_Log("AmbientOcclusion: Initialized %dx%d (AO: %dx%d)", width, height, ao_width_, ao_height_);
    return true;
}

void AmbientOcclusion::shutdown() {
    offscreen_color_.reset();
    offscreen_depth_.reset();
    ao_texture_.reset();
    ao_blurred_.reset();

    if (device_) {
        if (nearest_clamp_sampler_) {
            device_->release_sampler(nearest_clamp_sampler_);
            nearest_clamp_sampler_ = nullptr;
        }
        if (linear_clamp_sampler_) {
            device_->release_sampler(linear_clamp_sampler_);
            linear_clamp_sampler_ = nullptr;
        }
    }

    device_ = nullptr;
}

void AmbientOcclusion::resize(int width, int height) {
    if (width == width_ && height == height_) return;
    if (!device_) return;

    width_ = width;
    height_ = height;
    ao_width_ = std::max(1, width / 2);
    ao_height_ = std::max(1, height / 2);

    create_textures(width, height);
    SDL_Log("AmbientOcclusion: Resized to %dx%d (AO: %dx%d)", width, height, ao_width_, ao_height_);
}

void AmbientOcclusion::create_textures(int width, int height) {
    // Full-resolution offscreen color target
    offscreen_color_ = gpu::GPUTexture::create_2d(
        *device_, width, height,
        SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
        SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER);

    // Full-resolution depth
    offscreen_depth_ = gpu::GPUTexture::create_depth(*device_, width, height);

    // Half-resolution AO textures
    ao_texture_ = gpu::GPUTexture::create_2d(
        *device_, ao_width_, ao_height_,
        SDL_GPU_TEXTUREFORMAT_R8_UNORM,
        SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER);

    ao_blurred_ = gpu::GPUTexture::create_2d(
        *device_, ao_width_, ao_height_,
        SDL_GPU_TEXTUREFORMAT_R8_UNORM,
        SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER);
}

void AmbientOcclusion::create_samplers() {
    SDL_GPUSamplerCreateInfo nearest_info = {};
    nearest_info.min_filter = SDL_GPU_FILTER_NEAREST;
    nearest_info.mag_filter = SDL_GPU_FILTER_NEAREST;
    nearest_info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    nearest_info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    nearest_info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    nearest_info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    nearest_clamp_sampler_ = device_->create_sampler(nearest_info);

    SDL_GPUSamplerCreateInfo linear_info = nearest_info;
    linear_info.min_filter = SDL_GPU_FILTER_LINEAR;
    linear_info.mag_filter = SDL_GPU_FILTER_LINEAR;
    linear_clamp_sampler_ = device_->create_sampler(linear_info);
}

bool AmbientOcclusion::is_ready() const {
    return offscreen_color_ && offscreen_depth_ && ao_texture_ && ao_blurred_ &&
           nearest_clamp_sampler_ && linear_clamp_sampler_;
}

// =============================================================================
// Render Passes
// =============================================================================

SDL_GPURenderPass* AmbientOcclusion::begin_offscreen_pass(SDL_GPUCommandBuffer* cmd) {
    if (!is_ready()) return nullptr;

    SDL_GPUColorTargetInfo color_target = {};
    color_target.texture = offscreen_color_->handle();
    color_target.load_op = SDL_GPU_LOADOP_CLEAR;
    color_target.store_op = SDL_GPU_STOREOP_STORE;
    color_target.clear_color = {0.35f, 0.45f, 0.6f, 1.0f};

    SDL_GPUDepthStencilTargetInfo depth_target = {};
    depth_target.texture = offscreen_depth_->handle();
    depth_target.load_op = SDL_GPU_LOADOP_CLEAR;
    depth_target.store_op = SDL_GPU_STOREOP_STORE;
    depth_target.clear_depth = 1.0f;
    depth_target.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    depth_target.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;

    current_pass_ = SDL_BeginGPURenderPass(cmd, &color_target, 1, &depth_target);
    return current_pass_;
}

void AmbientOcclusion::end_offscreen_pass() {
    if (current_pass_) {
        SDL_EndGPURenderPass(current_pass_);
        current_pass_ = nullptr;
    }
}

void AmbientOcclusion::render_ssao_pass(SDL_GPUCommandBuffer* cmd, gpu::PipelineRegistry& pipelines,
                             const glm::mat4& projection, const glm::mat4& inv_projection) {
    auto* pipeline = pipelines.get_ssao_pipeline();
    if (!pipeline) return;

    gpu::GTAOUniforms uniforms = {};
    uniforms.projection = projection;
    uniforms.invProjection = inv_projection;
    uniforms.screenSize = glm::vec2(static_cast<float>(ao_width_), static_cast<float>(ao_height_));
    uniforms.invScreenSize = glm::vec2(1.0f / ao_width_, 1.0f / ao_height_);

    SDL_GPUColorTargetInfo color_target = {};
    color_target.texture = ao_texture_->handle();
    color_target.load_op = SDL_GPU_LOADOP_DONT_CARE;
    color_target.store_op = SDL_GPU_STOREOP_STORE;

    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &color_target, 1, nullptr);
    if (!pass) return;

    pipeline->bind(pass);
    SDL_PushGPUFragmentUniformData(cmd, 0, &uniforms, sizeof(uniforms));

    SDL_GPUTextureSamplerBinding depth_binding = {};
    depth_binding.texture = offscreen_depth_->handle();
    depth_binding.sampler = nearest_clamp_sampler_;
    SDL_BindGPUFragmentSamplers(pass, 0, &depth_binding, 1);

    SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
    SDL_EndGPURenderPass(pass);
}

void AmbientOcclusion::render_gtao_pass(SDL_GPUCommandBuffer* cmd, gpu::PipelineRegistry& pipelines,
                             const glm::mat4& projection, const glm::mat4& inv_projection) {
    auto* pipeline = pipelines.get_gtao_pipeline();
    if (!pipeline) return;

    gpu::GTAOUniforms uniforms = {};
    uniforms.projection = projection;
    uniforms.invProjection = inv_projection;
    uniforms.screenSize = glm::vec2(static_cast<float>(ao_width_), static_cast<float>(ao_height_));
    uniforms.invScreenSize = glm::vec2(1.0f / ao_width_, 1.0f / ao_height_);

    SDL_GPUColorTargetInfo color_target = {};
    color_target.texture = ao_texture_->handle();
    color_target.load_op = SDL_GPU_LOADOP_DONT_CARE;
    color_target.store_op = SDL_GPU_STOREOP_STORE;

    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &color_target, 1, nullptr);
    if (!pass) return;

    pipeline->bind(pass);
    SDL_PushGPUFragmentUniformData(cmd, 0, &uniforms, sizeof(uniforms));

    SDL_GPUTextureSamplerBinding depth_binding = {};
    depth_binding.texture = offscreen_depth_->handle();
    depth_binding.sampler = nearest_clamp_sampler_;
    SDL_BindGPUFragmentSamplers(pass, 0, &depth_binding, 1);

    SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
    SDL_EndGPURenderPass(pass);
}

void AmbientOcclusion::render_blur_pass(SDL_GPUCommandBuffer* cmd, gpu::PipelineRegistry& pipelines) {
    auto* pipeline = pipelines.get_blur_ao_pipeline();
    if (!pipeline) return;

    glm::vec2 inv_screen(1.0f / ao_width_, 1.0f / ao_height_);

    // Pass 1: Horizontal blur (ao_texture_ → ao_blurred_)
    {
        gpu::BlurUniforms uniforms = {};
        uniforms.direction = glm::vec2(1.0f, 0.0f);
        uniforms.invScreenSize = inv_screen;

        SDL_GPUColorTargetInfo color_target = {};
        color_target.texture = ao_blurred_->handle();
        color_target.load_op = SDL_GPU_LOADOP_DONT_CARE;
        color_target.store_op = SDL_GPU_STOREOP_STORE;

        SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &color_target, 1, nullptr);
        if (!pass) return;

        pipeline->bind(pass);
        SDL_PushGPUFragmentUniformData(cmd, 0, &uniforms, sizeof(uniforms));

        SDL_GPUTextureSamplerBinding bindings[2] = {};
        bindings[0].texture = ao_texture_->handle();
        bindings[0].sampler = nearest_clamp_sampler_;
        bindings[1].texture = offscreen_depth_->handle();
        bindings[1].sampler = nearest_clamp_sampler_;
        SDL_BindGPUFragmentSamplers(pass, 0, bindings, 2);

        SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
        SDL_EndGPURenderPass(pass);
    }

    // Pass 2: Vertical blur (ao_blurred_ → ao_texture_)
    {
        gpu::BlurUniforms uniforms = {};
        uniforms.direction = glm::vec2(0.0f, 1.0f);
        uniforms.invScreenSize = inv_screen;

        SDL_GPUColorTargetInfo color_target = {};
        color_target.texture = ao_texture_->handle();
        color_target.load_op = SDL_GPU_LOADOP_DONT_CARE;
        color_target.store_op = SDL_GPU_STOREOP_STORE;

        SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &color_target, 1, nullptr);
        if (!pass) return;

        pipeline->bind(pass);
        SDL_PushGPUFragmentUniformData(cmd, 0, &uniforms, sizeof(uniforms));

        SDL_GPUTextureSamplerBinding bindings[2] = {};
        bindings[0].texture = ao_blurred_->handle();
        bindings[0].sampler = nearest_clamp_sampler_;
        bindings[1].texture = offscreen_depth_->handle();
        bindings[1].sampler = nearest_clamp_sampler_;
        SDL_BindGPUFragmentSamplers(pass, 0, bindings, 2);

        SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
        SDL_EndGPURenderPass(pass);
    }
}

void AmbientOcclusion::render_composite_pass(SDL_GPUCommandBuffer* cmd, gpu::PipelineRegistry& pipelines,
                                  SDL_GPUTexture* swapchain_target) {
    auto* pipeline = pipelines.get_composite_pipeline();
    if (!pipeline || !swapchain_target) return;

    gpu::CompositeUniforms uniforms = {};

    SDL_GPUColorTargetInfo color_target = {};
    color_target.texture = swapchain_target;
    color_target.load_op = SDL_GPU_LOADOP_DONT_CARE;
    color_target.store_op = SDL_GPU_STOREOP_STORE;

    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &color_target, 1, nullptr);
    if (!pass) return;

    pipeline->bind(pass);
    SDL_PushGPUFragmentUniformData(cmd, 0, &uniforms, sizeof(uniforms));

    SDL_GPUTextureSamplerBinding bindings[2] = {};
    bindings[0].texture = offscreen_color_->handle();
    bindings[0].sampler = linear_clamp_sampler_;
    bindings[1].texture = ao_texture_->handle();  // After blur, result is back in ao_texture_
    bindings[1].sampler = linear_clamp_sampler_;
    SDL_BindGPUFragmentSamplers(pass, 0, bindings, 2);

    SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
    SDL_EndGPURenderPass(pass);
}

} // namespace mmo::engine::render
