#include "shadow_map.hpp"
#include "SDL3/SDL_gpu.h"
#include <SDL3/SDL_log.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

namespace mmo::engine::render {

ShadowMap::ShadowMap() = default;

ShadowMap::~ShadowMap() {
    shutdown();
}

bool ShadowMap::init(gpu::GPUDevice& device, int resolution) {
    device_ = &device;
    resolution_ = resolution;

    for (int i = 0; i < CSM_MAX_CASCADES; ++i) {
        cascade_textures_[i] = gpu::GPUTexture::create_depth(
            device, resolution_, resolution_);
        if (!cascade_textures_[i]) {
            SDL_Log("ShadowMap::init: Failed to create cascade depth texture %d", i);
            return false;
        }
    }

    SDL_GPUSamplerCreateInfo sampler_info = {};
    sampler_info.min_filter = SDL_GPU_FILTER_NEAREST;
    sampler_info.mag_filter = SDL_GPU_FILTER_NEAREST;
    sampler_info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    sampler_info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler_info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler_info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler_info.enable_compare = false;
    sampler_info.enable_anisotropy = false;

    shadow_sampler_ = device.create_sampler(sampler_info);
    if (!shadow_sampler_) {
        SDL_Log("ShadowMap::init: Failed to create shadow sampler");
        return false;
    }

    SDL_Log("ShadowMap: Initialized %dx%d x %d cascades", resolution_, resolution_, active_cascades_);
    return true;
}

void ShadowMap::shutdown() {
    if (device_ && shadow_sampler_) {
        device_->release_sampler(shadow_sampler_);
        shadow_sampler_ = nullptr;
    }
    for (auto& tex : cascade_textures_) {
        tex.reset();
    }
    device_ = nullptr;
}

bool ShadowMap::reinit(int resolution) {
    if (!device_) return false;
    if (resolution == resolution_) return true;

    // Destroy old textures
    for (auto& tex : cascade_textures_) {
        tex.reset();
    }

    resolution_ = resolution;

    // Recreate textures at new resolution
    for (int i = 0; i < CSM_MAX_CASCADES; ++i) {
        cascade_textures_[i] = gpu::GPUTexture::create_depth(
            *device_, resolution_, resolution_);
        if (!cascade_textures_[i]) {
            SDL_Log("ShadowMap::reinit: Failed to create cascade depth texture %d", i);
            return false;
        }
    }

    SDL_Log("ShadowMap: Reinitialized at %dx%d", resolution_, resolution_);
    return true;
}

void ShadowMap::set_active_cascades(int count) {
    active_cascades_ = std::clamp(count, 1, CSM_MAX_CASCADES);
}

void ShadowMap::compute_cascade_splits(float near_plane, float far_plane) {
    float ratio = far_plane / near_plane;
    for (int i = 0; i < active_cascades_; ++i) {
        float p = static_cast<float>(i + 1) / static_cast<float>(active_cascades_);
        float log_split = near_plane * std::pow(ratio, p);
        float uniform_split = near_plane + (far_plane - near_plane) * p;
        cascades_[i].split_depth = split_lambda * log_split + (1.0f - split_lambda) * uniform_split;
    }
}

glm::mat4 ShadowMap::compute_cascade_matrix(const glm::mat4& camera_view, const glm::mat4& camera_proj,
                                              const glm::vec3& light_dir,
                                              float near_split, float far_split) {
    glm::mat4 slice_proj = camera_proj;

    float n = near_split;
    float f = far_split;
    slice_proj[2][2] = f / (n - f);
    slice_proj[3][2] = -(f * n) / (f - n);

    glm::mat4 inv_vp = glm::inverse(slice_proj * camera_view);

    // 8 NDC corners: x,y in [-1,1], z in [0,1] (depth zero to one)
    static const glm::vec4 ndc_corners[8] = {
        {-1, -1, 0, 1}, { 1, -1, 0, 1}, {-1,  1, 0, 1}, { 1,  1, 0, 1},
        {-1, -1, 1, 1}, { 1, -1, 1, 1}, {-1,  1, 1, 1}, { 1,  1, 1, 1},
    };

    glm::vec3 world_corners[8];
    glm::vec3 center(0.0f);
    for (int i = 0; i < 8; ++i) {
        glm::vec4 wc = inv_vp * ndc_corners[i];
        world_corners[i] = glm::vec3(wc) / wc.w;
        center += world_corners[i];
    }
    center /= 8.0f;

    glm::vec3 light_direction = glm::normalize(light_dir);
    glm::vec3 up = glm::vec3(0, 1, 0);
    if (std::abs(glm::dot(light_direction, up)) > 0.99f) {
        up = glm::vec3(1, 0, 0);
    }

    glm::vec3 eye = center - light_direction * 500.0f;
    glm::mat4 light_view = glm::lookAt(eye, center, up);

    glm::vec3 min_ls(std::numeric_limits<float>::max());
    glm::vec3 max_ls(std::numeric_limits<float>::lowest());
    for (int i = 0; i < 8; ++i) {
        glm::vec3 ls = glm::vec3(light_view * glm::vec4(world_corners[i], 1.0f));
        min_ls = glm::min(min_ls, ls);
        max_ls = glm::max(max_ls, ls);
    }

    min_ls.z -= 500.0f;
    max_ls.z += 500.0f;

    float texel_size_x = (max_ls.x - min_ls.x) / static_cast<float>(resolution_);
    float texel_size_y = (max_ls.y - min_ls.y) / static_cast<float>(resolution_);
    if (texel_size_x > 0.0f) {
        min_ls.x = std::floor(min_ls.x / texel_size_x) * texel_size_x;
        max_ls.x = std::floor(max_ls.x / texel_size_x) * texel_size_x;
    }
    if (texel_size_y > 0.0f) {
        min_ls.y = std::floor(min_ls.y / texel_size_y) * texel_size_y;
        max_ls.y = std::floor(max_ls.y / texel_size_y) * texel_size_y;
    }

    float ortho_near = -max_ls.z;
    float ortho_far = -min_ls.z;
    glm::mat4 light_proj = glm::orthoRH_ZO(min_ls.x, max_ls.x, min_ls.y, max_ls.y, ortho_near, ortho_far);
    return light_proj * light_view;
}

void ShadowMap::update(const glm::mat4& camera_view, const glm::mat4& camera_proj,
                        const glm::vec3& light_dir, float near_plane, float far_plane) {
    compute_cascade_splits(near_plane, far_plane);

    float prev_split = near_plane;
    for (int i = 0; i < active_cascades_; ++i) {
        cascades_[i].light_view_projection = compute_cascade_matrix(
            camera_view, camera_proj, light_dir, prev_split, cascades_[i].split_depth);
        prev_split = cascades_[i].split_depth;
    }
}

SDL_GPURenderPass* ShadowMap::begin_shadow_pass(SDL_GPUCommandBuffer* cmd, int cascade_index) {
    if (!cmd || cascade_index < 0 || cascade_index >= active_cascades_) {
        return nullptr;
    }
    if (!cascade_textures_[cascade_index]) {
        return nullptr;
    }

    SDL_GPUDepthStencilTargetInfo depth_target = {};
    depth_target.texture = cascade_textures_[cascade_index]->handle();
    depth_target.load_op = SDL_GPU_LOADOP_CLEAR;
    depth_target.store_op = SDL_GPU_STOREOP_STORE;
    depth_target.clear_depth = 1.0f;
    depth_target.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    depth_target.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;

    current_shadow_pass_ = SDL_BeginGPURenderPass(cmd, nullptr, 0, &depth_target);
    if (!current_shadow_pass_) {
        SDL_Log("ShadowMap::begin_shadow_pass: Failed to begin render pass for cascade %d", cascade_index);
        return nullptr;
    }

    SDL_GPUViewport viewport = {};
    viewport.x = 0;
    viewport.y = 0;
    viewport.w = static_cast<float>(resolution_);
    viewport.h = static_cast<float>(resolution_);
    viewport.min_depth = 0.0f;
    viewport.max_depth = 1.0f;
    SDL_SetGPUViewport(current_shadow_pass_, &viewport);

    SDL_Rect scissor = {};
    scissor.x = 0;
    scissor.y = 0;
    scissor.w = resolution_;
    scissor.h = resolution_;
    SDL_SetGPUScissor(current_shadow_pass_, &scissor);

    return current_shadow_pass_;
}

void ShadowMap::end_shadow_pass() {
    if (current_shadow_pass_) {
        SDL_EndGPURenderPass(current_shadow_pass_);
        current_shadow_pass_ = nullptr;
    }
}

SDL_GPUTexture* ShadowMap::shadow_texture(int cascade) const {
    if (cascade < 0 || cascade >= CSM_MAX_CASCADES || !cascade_textures_[cascade]) {
        return nullptr;
    }
    return cascade_textures_[cascade]->handle();
}

bool ShadowMap::is_ready() const {
    for (int i = 0; i < active_cascades_; ++i) {
        if (!cascade_textures_[i]) return false;
    }
    return shadow_sampler_ != nullptr;
}

gpu::ShadowDataUniforms ShadowMap::get_shadow_uniforms(int shadow_mode) const {
    gpu::ShadowDataUniforms uniforms = {};
    for (int i = 0; i < active_cascades_; ++i) {
        uniforms.lightViewProjection[i] = cascades_[i].light_view_projection;
    }
    // Set cascade splits - unused cascades get max float so they're never selected
    float splits[4] = {
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max()
    };
    for (int i = 0; i < active_cascades_; ++i) {
        splits[i] = cascades_[i].split_depth;
    }
    uniforms.cascadeSplits = glm::vec4(splits[0], splits[1], splits[2], splits[3]);
    uniforms.shadowMapResolution = static_cast<float>(resolution_);
    uniforms.lightSize = light_size;
    uniforms.shadowEnabled = static_cast<float>(shadow_mode);
    return uniforms;
}

} // namespace mmo::engine::render
