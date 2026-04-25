#include "volumetric_fog.hpp"
#include "engine/gpu/gpu_pipeline.hpp"
#include <algorithm>
#include <glm/gtc/matrix_inverse.hpp>
#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_log.h>

namespace mmo::engine::render {

VolumetricFog::VolumetricFog() = default;

VolumetricFog::~VolumetricFog() {
    shutdown();
}

bool VolumetricFog::init(gpu::GPUDevice& device, int width, int height) {
    device_ = &device;
    width_ = width;
    height_ = height;
    fog_width_ = std::max(1, width / 2);
    fog_height_ = std::max(1, height / 2);

    create_textures(width, height);
    create_sampler();

    if (!is_ready()) {
        SDL_Log("VolumetricFog: Failed to initialize");
        return false;
    }

    SDL_Log("VolumetricFog: Initialized %dx%d (fog: %dx%d)", width, height, fog_width_, fog_height_);
    return true;
}

void VolumetricFog::shutdown() {
    fog_texture_.reset();

    if (device_ && linear_sampler_) {
        device_->release_sampler(linear_sampler_);
        linear_sampler_ = nullptr;
    }

    device_ = nullptr;
}

void VolumetricFog::resize(int width, int height) {
    if (width == width_ && height == height_) {
        return;
    }
    if (!device_) {
        return;
    }

    width_ = width;
    height_ = height;
    fog_width_ = std::max(1, width / 2);
    fog_height_ = std::max(1, height / 2);

    create_textures(width, height);
    SDL_Log("VolumetricFog: Resized to %dx%d (fog: %dx%d)", width, height, fog_width_, fog_height_);
}

void VolumetricFog::create_textures(int width, int height) {
    (void)width;
    (void)height;
    fog_texture_ =
        gpu::GPUTexture::create_2d(*device_, fog_width_, fog_height_, SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT,
                                   SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER);
}

void VolumetricFog::create_sampler() {
    SDL_GPUSamplerCreateInfo info = {};
    info.min_filter = SDL_GPU_FILTER_LINEAR;
    info.mag_filter = SDL_GPU_FILTER_LINEAR;
    info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    linear_sampler_ = device_->create_sampler(info);
}

bool VolumetricFog::is_ready() const {
    return fog_texture_ && (linear_sampler_ != nullptr);
}

SDL_GPUTexture* VolumetricFog::fog_texture() const {
    if (fog_texture_) {
        return fog_texture_->handle();
    }
    return nullptr;
}

void VolumetricFog::render(SDL_GPUCommandBuffer* cmd, gpu::PipelineRegistry& registry, SDL_GPUTexture* depth_texture,
                           const ShadowMap& shadow_map, const scene::CameraState& camera, const glm::vec3& light_dir,
                           bool god_rays_enabled, bool fog_enabled, float density_multiplier) {
    if (!is_ready() || !cmd || !depth_texture) {
        return;
    }

    auto* pipeline = registry.get_pipeline(gpu::PipelineType::VolumetricFog);
    if (!pipeline) {
        return;
    }

    // Build uniforms
    gpu::VolumetricFogUniforms uniforms = {};
    if (camera.view_projection != cached_view_projection_) {
        cached_view_projection_ = camera.view_projection;
        cached_inv_view_projection_ = glm::inverse(camera.view_projection);
    }
    uniforms.invViewProjection = cached_inv_view_projection_;
    // Use cascade 0 (highest resolution) for god rays
    if (shadow_map.is_ready()) {
        uniforms.shadowLightViewProjection = shadow_map.cascades()[0].light_view_projection;
    }
    uniforms.lightDir = light_dir;
    uniforms.fogDensity = 0.0015f;
    uniforms.lightColor = glm::vec3(1.0f, 0.9f, 0.7f);
    uniforms.scatterStrength = 0.25f;
    uniforms.fogColor = glm::vec3(0.5f, 0.6f, 0.7f);
    uniforms.fogHeight = 100.0f;
    uniforms.cameraPos = camera.position;
    uniforms.fogFalloff = 0.005f;
    uniforms.nearPlane = 0.1f;
    uniforms.farPlane = 5000.0f;
    uniforms.godRaysEnabled = god_rays_enabled ? 1.0f : 0.0f;
    uniforms.densityMultiplier = density_multiplier;
    if (!fog_enabled) {
        uniforms.fogDensity = 0.0f;
    }

    // Begin render pass to fog texture
    SDL_GPUColorTargetInfo color_target = {};
    color_target.texture = fog_texture_->handle();
    color_target.load_op = SDL_GPU_LOADOP_DONT_CARE;
    color_target.store_op = SDL_GPU_STOREOP_STORE;

    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &color_target, 1, nullptr);
    if (!pass) {
        return;
    }

    pipeline->bind(pass);
    SDL_PushGPUFragmentUniformData(cmd, 0, &uniforms, sizeof(uniforms));

    // Bind depth texture (slot 0) and shadow map (slot 1)
    SDL_GPUTextureSamplerBinding bindings[2] = {};
    bindings[0].texture = depth_texture;
    bindings[0].sampler = linear_sampler_;
    bindings[1].texture = shadow_map.shadow_texture(0);
    bindings[1].sampler = shadow_map.shadow_sampler();
    SDL_BindGPUFragmentSamplers(pass, 0, bindings, 2);

    SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
    SDL_EndGPURenderPass(pass);
}

} // namespace mmo::engine::render
