#include "world_renderer.hpp"
#include <algorithm>
#include "../gpu/gpu_texture.hpp"
#include "../gpu/gpu_uniforms.hpp"
#include "../render_constants.hpp"
#include "SDL3/SDL_error.h"
#include "SDL3/SDL_gpu.h"
#include "engine/gpu/gpu_buffer.hpp"
#include "engine/gpu/gpu_device.hpp"
#include "engine/gpu/pipeline_registry.hpp"
#include "engine/model_loader.hpp"
#include "glm/ext/matrix_float3x3.hpp"
#include "glm/ext/matrix_float4x4.hpp"
#include "glm/ext/matrix_transform.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "glm/matrix.hpp"
#include "glm/trigonometric.hpp"
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

namespace mmo::engine::render {

namespace gpu = mmo::engine::gpu;

WorldRenderer::WorldRenderer() = default;

WorldRenderer::~WorldRenderer() {
    shutdown();
}

bool WorldRenderer::init(gpu::GPUDevice& device, gpu::PipelineRegistry& pipeline_registry,
                         float world_width, float world_height, ModelManager* model_manager) {
    device_ = &device;
    pipeline_registry_ = &pipeline_registry;
    world_width_ = world_width;
    world_height_ = world_height;
    model_manager_ = model_manager;
    
    // Create sampler for textures
    SDL_GPUSamplerCreateInfo sampler_info = {};
    sampler_info.min_filter = SDL_GPU_FILTER_LINEAR;
    sampler_info.mag_filter = SDL_GPU_FILTER_LINEAR;
    sampler_info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
    sampler_info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    sampler_info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    sampler_info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    sampler_info.max_anisotropy = 16.0f;
    sampler_info.enable_anisotropy = true;
    sampler_ = SDL_CreateGPUSampler(device_->handle(), &sampler_info);
    if (!sampler_) {
        std::cerr << "Failed to create sampler: " << SDL_GetError() << std::endl;
        return false;
    }
    
    create_skybox_mesh();
    create_grid_mesh();

    return true;
}

void WorldRenderer::shutdown() {
    skybox_vertex_buffer_.reset();
    grid_vertex_buffer_.reset();
    
    if (sampler_ && device_) {
        SDL_ReleaseGPUSampler(device_->handle(), sampler_);
        sampler_ = nullptr;
    }
    
    device_ = nullptr;
    pipeline_registry_ = nullptr;
}

void WorldRenderer::update(float dt) {
    skybox_time_ += dt;
}

float WorldRenderer::get_terrain_height(float x, float z) const {
    if (terrain_height_func_) {
        return terrain_height_func_(x, z);
    }
    return 0.0f;
}

void WorldRenderer::create_skybox_mesh() {
    if (!device_) return;
    
    // Fullscreen triangle in clip space (3 vertices that cover the entire screen)
    // z component is unused by shader but included for float3 vertex format
    float vertices[] = {
        -1.0f, -1.0f, 0.0f,
         3.0f, -1.0f, 0.0f,
        -1.0f,  3.0f, 0.0f,
    };
    
    skybox_vertex_buffer_ = gpu::GPUBuffer::create_static(
        *device_, gpu::GPUBuffer::Type::Vertex, vertices, sizeof(vertices));
    
    if (!skybox_vertex_buffer_) {
        std::cerr << "Failed to create skybox vertex buffer" << std::endl;
    }
}

void WorldRenderer::create_grid_mesh() {
    if (!device_) return;
    
    std::vector<float> grid_data;
    float grid_step = 100.0f;
    
    // Grid lines
    for (float x = 0; x <= world_width_; x += grid_step) {
        grid_data.insert(grid_data.end(), {x, 0.0f, 0.0f, 0.15f, 0.15f, 0.2f, 0.8f});
        grid_data.insert(grid_data.end(), {x, 0.0f, world_height_, 0.15f, 0.15f, 0.2f, 0.8f});
    }
    for (float z = 0; z <= world_height_; z += grid_step) {
        grid_data.insert(grid_data.end(), {0.0f, 0.0f, z, 0.15f, 0.15f, 0.2f, 0.8f});
        grid_data.insert(grid_data.end(), {world_width_, 0.0f, z, 0.15f, 0.15f, 0.2f, 0.8f});
    }
    
    // World boundary
    grid_data.insert(grid_data.end(), {0.0f, 0.0f, 0.0f, 0.4f, 0.4f, 0.5f, 1.0f});
    grid_data.insert(grid_data.end(), {world_width_, 0.0f, 0.0f, 0.4f, 0.4f, 0.5f, 1.0f});
    grid_data.insert(grid_data.end(), {world_width_, 0.0f, 0.0f, 0.4f, 0.4f, 0.5f, 1.0f});
    grid_data.insert(grid_data.end(), {world_width_, 0.0f, world_height_, 0.4f, 0.4f, 0.5f, 1.0f});
    grid_data.insert(grid_data.end(), {world_width_, 0.0f, world_height_, 0.4f, 0.4f, 0.5f, 1.0f});
    grid_data.insert(grid_data.end(), {0.0f, 0.0f, world_height_, 0.4f, 0.4f, 0.5f, 1.0f});
    grid_data.insert(grid_data.end(), {0.0f, 0.0f, world_height_, 0.4f, 0.4f, 0.5f, 1.0f});
    grid_data.insert(grid_data.end(), {0.0f, 0.0f, 0.0f, 0.4f, 0.4f, 0.5f, 1.0f});
    
    grid_vertex_count_ = static_cast<uint32_t>(grid_data.size() / 7);
    
    grid_vertex_buffer_ = gpu::GPUBuffer::create_static(
        *device_, gpu::GPUBuffer::Type::Vertex, 
        grid_data.data(), grid_data.size() * sizeof(float));
    
    if (!grid_vertex_buffer_) {
        std::cerr << "Failed to create grid vertex buffer" << std::endl;
    }
}

void WorldRenderer::render_skybox(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd,
                                   const glm::mat4& view, const glm::mat4& projection) {
    if (!skybox_vertex_buffer_ || !pipeline_registry_ || !pass || !cmd) return;
    
    auto* pipeline = pipeline_registry_->get_skybox_pipeline();
    if (!pipeline) return;
    
    pipeline->bind(pass);
    
    // Push fragment uniforms - invVP for per-pixel ray, plus time and sun direction
    gpu::SkyboxFragmentUniforms fs_uniforms = {};
    glm::mat4 view_no_translation = glm::mat4(glm::mat3(view));
    fs_uniforms.invViewProjection = glm::inverse(projection * view_no_translation);
    fs_uniforms.time = skybox_time_;
    fs_uniforms.sunDirection = sun_direction_;
    SDL_PushGPUFragmentUniformData(cmd, 0, &fs_uniforms, sizeof(fs_uniforms));
    
    // Bind vertex buffer
    SDL_GPUBufferBinding vb_binding = {};
    vb_binding.buffer = skybox_vertex_buffer_->handle();
    vb_binding.offset = 0;
    SDL_BindGPUVertexBuffers(pass, 0, &vb_binding, 1);
    
    // Draw fullscreen triangle (3 vertices)
    SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
}

void WorldRenderer::render_grid(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd,
                                 const glm::mat4& view, const glm::mat4& projection) {
    if (!grid_vertex_buffer_ || !pipeline_registry_ || !pass || !cmd) return;
    
    auto* pipeline = pipeline_registry_->get_grid_pipeline();
    if (!pipeline) return;
    
    pipeline->bind(pass);
    
    // Push uniforms
    gpu::GridVertexUniforms uniforms = {};
    uniforms.viewProjection = projection * view;
    SDL_PushGPUVertexUniformData(cmd, 0, &uniforms, sizeof(uniforms));
    
    // Bind vertex buffer
    SDL_GPUBufferBinding vb_binding = {};
    vb_binding.buffer = grid_vertex_buffer_->handle();
    vb_binding.offset = 0;
    SDL_BindGPUVertexBuffers(pass, 0, &vb_binding, 1);
    
    // Draw grid lines
    SDL_DrawGPUPrimitives(pass, grid_vertex_count_, 1, 0, 0);
}

} // namespace mmo::engine::render
