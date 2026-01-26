#include "shadow_system.hpp"
#include "../gpu/gpu_pipeline.hpp"
#include <SDL3/SDL_log.h>
#include <glm/gtc/matrix_transform.hpp>
#include <cstdlib>

namespace mmo {

// ============================================================================
// ShadowSystem - SDL3 GPU Implementation
// ============================================================================

ShadowSystem::ShadowSystem() = default;

ShadowSystem::~ShadowSystem() {
    shutdown();
}

bool ShadowSystem::init(gpu::GPUDevice& device, gpu::PipelineRegistry& pipeline_registry, 
                        int shadow_map_size) {
    device_ = &device;
    pipeline_registry_ = &pipeline_registry;
    shadow_map_size_ = shadow_map_size;
    
    SDL_Log("ShadowSystem: Initializing shadow mapping with %dx%d shadow map...", 
            shadow_map_size_, shadow_map_size_);
    
    // Create shadow depth texture as a render target
    shadow_map_ = gpu::GPUTexture::create_depth(*device_, shadow_map_size_, shadow_map_size_);
    if (!shadow_map_) {
        SDL_Log("ShadowSystem: Failed to create shadow depth texture");
        return false;
    }
    
    // Create shadow comparison sampler for PCF filtering
    SDL_GPUSamplerCreateInfo sampler_info = {};
    sampler_info.min_filter = SDL_GPU_FILTER_LINEAR;
    sampler_info.mag_filter = SDL_GPU_FILTER_LINEAR;
    sampler_info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    sampler_info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler_info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler_info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler_info.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
    sampler_info.enable_compare = true;
    
    shadow_sampler_ = device_->create_sampler(sampler_info);
    if (!shadow_sampler_) {
        SDL_Log("ShadowSystem: Failed to create shadow sampler");
        return false;
    }
    
    // Get shadow pipelines from registry (created lazily)
    shadow_pipeline_ = pipeline_registry_->get_shadow_pipeline();
    skinned_shadow_pipeline_ = pipeline_registry_->get_skinned_shadow_pipeline();
    
    if (!shadow_pipeline_) {
        SDL_Log("ShadowSystem: Warning - Shadow pipeline not available");
    }
    if (!skinned_shadow_pipeline_) {
        SDL_Log("ShadowSystem: Warning - Skinned shadow pipeline not available");
    }
    
    SDL_Log("ShadowSystem: Shadow mapping initialized successfully");
    return true;
}

void ShadowSystem::shutdown() {
    shadow_pipeline_ = nullptr;
    skinned_shadow_pipeline_ = nullptr;
    
    if (shadow_sampler_ && device_) {
        device_->release_sampler(shadow_sampler_);
        shadow_sampler_ = nullptr;
    }
    
    shadow_map_.reset();
    pipeline_registry_ = nullptr;
    device_ = nullptr;
}

void ShadowSystem::update_light_space_matrix(float camera_x, float camera_z, const glm::vec3& light_dir) {
    // Use known terrain height bounds from heightmap config (-500 to 500)
    // Plus margin for objects on terrain
    constexpr float MIN_TERRAIN = -500.0f - 100.0f;   // Below terrain
    constexpr float MAX_TERRAIN = 500.0f + 600.0f;    // Above for trees/buildings
    
    float center_height = (MIN_TERRAIN + MAX_TERRAIN) * 0.5f;
    float height_range = MAX_TERRAIN - MIN_TERRAIN;
    
    // Position light to look at the center of the shadow volume
    glm::vec3 center = glm::vec3(camera_x, center_height, camera_z);
    glm::vec3 light_pos = center - light_dir * (shadow_distance_ + height_range);
    glm::mat4 light_view = glm::lookAt(light_pos, center, glm::vec3(0.0f, 1.0f, 0.0f));
    
    // Orthographic projection covering the full height range
    float near_plane = 1.0f;
    float far_plane = 2.0f * (shadow_distance_ + height_range) + height_range;
    
    glm::mat4 light_projection = glm::ortho(
        -shadow_distance_, shadow_distance_,
        -shadow_distance_, shadow_distance_,
        near_plane, far_plane
    );
    
    light_space_matrix_ = light_projection * light_view;
}

SDL_GPURenderPass* ShadowSystem::begin_shadow_pass(SDL_GPUCommandBuffer* cmd) {
    if (!enabled_ || !shadow_map_ || !cmd) {
        return nullptr;
    }
    
    // Create depth-only render pass targeting the shadow map
    SDL_GPUDepthStencilTargetInfo depth_target = {};
    depth_target.texture = shadow_map_->handle();
    depth_target.load_op = SDL_GPU_LOADOP_CLEAR;
    depth_target.store_op = SDL_GPU_STOREOP_STORE;
    depth_target.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    depth_target.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
    depth_target.clear_depth = 1.0f;
    depth_target.cycle = true;
    
    // No color targets for shadow pass
    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, nullptr, 0, &depth_target);
    if (!pass) {
        SDL_Log("ShadowSystem: Failed to begin shadow render pass: %s", SDL_GetError());
        return nullptr;
    }
    
    // Set viewport to shadow map size
    SDL_GPUViewport viewport = {};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.w = static_cast<float>(shadow_map_size_);
    viewport.h = static_cast<float>(shadow_map_size_);
    viewport.min_depth = 0.0f;
    viewport.max_depth = 1.0f;
    SDL_SetGPUViewport(pass, &viewport);
    
    // Set scissor rect
    SDL_Rect scissor = {};
    scissor.x = 0;
    scissor.y = 0;
    scissor.w = shadow_map_size_;
    scissor.h = shadow_map_size_;
    SDL_SetGPUScissor(pass, &scissor);
    
    return pass;
}

void ShadowSystem::end_shadow_pass(SDL_GPURenderPass* pass) {
    if (pass) {
        SDL_EndGPURenderPass(pass);
    }
}

void ShadowSystem::render_shadow_caster(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd,
                                         const glm::mat4& model_matrix,
                                         gpu::GPUBuffer* vertex_buffer,
                                         gpu::GPUBuffer* index_buffer,
                                         uint32_t index_count) {
    if (!pass || !shadow_pipeline_ || !vertex_buffer) {
        return;
    }
    
    // Bind shadow pipeline
    shadow_pipeline_->bind(pass);
    
    // Push uniforms
    ShadowUniforms uniforms;
    uniforms.light_space_matrix = light_space_matrix_;
    uniforms.model = model_matrix;
    SDL_PushGPUVertexUniformData(cmd, 0, &uniforms, sizeof(ShadowUniforms));
    
    // Bind vertex buffer
    SDL_GPUBufferBinding vb_binding = {};
    vb_binding.buffer = vertex_buffer->handle();
    vb_binding.offset = 0;
    SDL_BindGPUVertexBuffers(pass, 0, &vb_binding, 1);
    
    // Draw with or without index buffer
    if (index_buffer) {
        SDL_GPUBufferBinding ib_binding = {};
        ib_binding.buffer = index_buffer->handle();
        ib_binding.offset = 0;
        SDL_BindGPUIndexBuffer(pass, &ib_binding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
        SDL_DrawGPUIndexedPrimitives(pass, index_count, 1, 0, 0, 0);
    } else {
        SDL_DrawGPUPrimitives(pass, index_count, 1, 0, 0);
    }
}

void ShadowSystem::render_skinned_shadow_caster(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd,
                                                 const glm::mat4& model_matrix,
                                                 const glm::mat4* bone_matrices, uint32_t bone_count,
                                                 gpu::GPUBuffer* vertex_buffer,
                                                 gpu::GPUBuffer* index_buffer,
                                                 uint32_t index_count) {
    if (!pass || !skinned_shadow_pipeline_ || !vertex_buffer || !bone_matrices) {
        return;
    }
    
    // Bind skinned shadow pipeline
    skinned_shadow_pipeline_->bind(pass);
    
    // Push transform uniforms
    ShadowUniforms uniforms;
    uniforms.light_space_matrix = light_space_matrix_;
    uniforms.model = model_matrix;
    SDL_PushGPUVertexUniformData(cmd, 0, &uniforms, sizeof(ShadowUniforms));
    
    // Push bone matrices
    BoneUniforms bone_uniforms;
    uint32_t copy_count = (bone_count < MAX_BONES) ? bone_count : MAX_BONES;
    for (uint32_t i = 0; i < copy_count; ++i) {
        bone_uniforms.bones[i] = bone_matrices[i];
    }
    // Initialize remaining bones to identity
    for (uint32_t i = copy_count; i < MAX_BONES; ++i) {
        bone_uniforms.bones[i] = glm::mat4(1.0f);
    }
    SDL_PushGPUVertexUniformData(cmd, 1, &bone_uniforms, sizeof(BoneUniforms));
    
    // Bind vertex buffer
    SDL_GPUBufferBinding vb_binding = {};
    vb_binding.buffer = vertex_buffer->handle();
    vb_binding.offset = 0;
    SDL_BindGPUVertexBuffers(pass, 0, &vb_binding, 1);
    
    // Draw with index buffer
    if (index_buffer) {
        SDL_GPUBufferBinding ib_binding = {};
        ib_binding.buffer = index_buffer->handle();
        ib_binding.offset = 0;
        SDL_BindGPUIndexBuffer(pass, &ib_binding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
        SDL_DrawGPUIndexedPrimitives(pass, index_count, 1, 0, 0, 0);
    } else {
        SDL_DrawGPUPrimitives(pass, index_count, 1, 0, 0);
    }
}

// ============================================================================
// SSAOSystem - SDL3 GPU Implementation (Stub)
// SSAO requires a full deferred rendering pipeline which will be implemented
// as part of a future task. For now, this is a minimal stub that allows
// the system to be initialized but remains disabled.
// ============================================================================

SSAOSystem::SSAOSystem() = default;

SSAOSystem::~SSAOSystem() {
    shutdown();
}

bool SSAOSystem::init(gpu::GPUDevice& device, int width, int height) {
    device_ = &device;
    width_ = width;
    height_ = height;
    
    SDL_Log("SSAOSystem: SSAO initialization (stub) - disabled until deferred rendering pipeline is ready");
    
    // Generate SSAO kernel for future use
    generate_kernel();
    
    // Create a basic sampler for when SSAO is implemented
    SDL_GPUSamplerCreateInfo sampler_info = {};
    sampler_info.min_filter = SDL_GPU_FILTER_LINEAR;
    sampler_info.mag_filter = SDL_GPU_FILTER_LINEAR;
    sampler_info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    sampler_info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler_info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler_info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    
    ssao_sampler_ = device_->create_sampler(sampler_info);
    
    // SSAO is disabled by default until full implementation
    enabled_ = false;
    
    return true;
}

void SSAOSystem::shutdown() {
    if (ssao_sampler_ && device_) {
        device_->release_sampler(ssao_sampler_);
        ssao_sampler_ = nullptr;
    }
    
    gbuffer_position_.reset();
    gbuffer_normal_.reset();
    gbuffer_depth_.reset();
    ssao_texture_.reset();
    ssao_blur_texture_.reset();
    ssao_noise_.reset();
    
    ssao_kernel_.clear();
    device_ = nullptr;
}

void SSAOSystem::resize(int width, int height) {
    if (width == width_ && height == height_) return;
    
    width_ = width;
    height_ = height;
    
    // When SSAO is fully implemented, recreate G-buffer and SSAO textures here
}

void SSAOSystem::generate_kernel() {
    ssao_kernel_.clear();
    ssao_kernel_.reserve(64);
    
    for (int i = 0; i < 64; ++i) {
        glm::vec3 sample(
            static_cast<float>(rand()) / RAND_MAX * 2.0f - 1.0f,
            static_cast<float>(rand()) / RAND_MAX * 2.0f - 1.0f,
            static_cast<float>(rand()) / RAND_MAX
        );
        sample = glm::normalize(sample);
        sample *= static_cast<float>(rand()) / RAND_MAX;
        
        // Scale samples to be closer to the origin
        float scale = static_cast<float>(i) / 64.0f;
        scale = 0.1f + scale * scale * 0.9f;
        sample *= scale;
        
        ssao_kernel_.push_back(sample);
    }
}

} // namespace mmo
