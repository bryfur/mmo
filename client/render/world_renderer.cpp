#include "world_renderer.hpp"
#include "../gpu/gpu_texture.hpp"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <glm/gtc/matrix_transform.hpp>

namespace mmo {

// Uniform structures for shader data (must match HLSL shaders in pipeline_registry.cpp)

// Skybox vertex shader uniforms (b0): only view_projection matrix
struct alignas(16) SkyboxVertexUniforms {
    glm::mat4 view_projection;
};

// Skybox fragment shader uniforms (b0): sky colors
struct alignas(16) SkyboxFragmentUniforms {
    glm::vec3 sky_color_top;
    float _padding1;
    glm::vec3 sky_color_bottom;
    float _padding2;
};

// Grid vertex shader uniforms
struct alignas(16) GridUniforms {
    glm::mat4 view_projection;
};

// Model vertex shader uniforms (b0): matches pipeline_registry model_vertex
struct alignas(16) ModelVertexUniforms {
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 projection;
    glm::vec3 camera_pos;
    float _padding0;
    glm::mat4 light_space_matrix;
};

// Model fragment shader uniforms (b0): matches pipeline_registry model_fragment
struct alignas(16) ModelFragmentUniforms {
    glm::vec3 light_dir;
    float ambient;
    glm::vec3 light_color;
    float _padding1;
    glm::vec4 tint_color;
    glm::vec3 fog_color;
    float fog_start;
    float fog_end;
    int has_texture;
    int shadows_enabled;
    int fog_enabled;
};

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
    generate_mountain_positions();
    
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

// Legacy init function for backward compatibility during migration
bool WorldRenderer::init(float world_width, float world_height, ModelManager* model_manager) {
    world_width_ = world_width;
    world_height_ = world_height;
    model_manager_ = model_manager;
    
    // Note: Without a GPUDevice, we can't create the GPU buffers.
    // For now, just generate the mountain positions which don't require GPU.
    // This allows existing OpenGL code to continue working (render methods will be no-ops)
    // until call sites in renderer.cpp are migrated to use the new API.
    generate_mountain_positions();
    
    // Log a one-time warning about the partial initialization
    static bool warned = false;
    if (!warned) {
        std::cerr << "[WorldRenderer] Legacy init() called - GPU rendering disabled. "
                  << "Call init(GPUDevice&, PipelineRegistry&, ...) to enable SDL3 GPU rendering." 
                  << std::endl;
        warned = true;
    }
    
    return true;
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
    
    float vertices[] = {
        // Back face
        -1.0f, -1.0f, -1.0f,  1.0f,  1.0f, -1.0f,  1.0f, -1.0f, -1.0f,
         1.0f,  1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f,  1.0f, -1.0f,
        // Front face
        -1.0f, -1.0f,  1.0f,  1.0f, -1.0f,  1.0f,  1.0f,  1.0f,  1.0f,
         1.0f,  1.0f,  1.0f, -1.0f,  1.0f,  1.0f, -1.0f, -1.0f,  1.0f,
        // Left face
        -1.0f,  1.0f,  1.0f, -1.0f,  1.0f, -1.0f, -1.0f, -1.0f, -1.0f,
        -1.0f, -1.0f, -1.0f, -1.0f, -1.0f,  1.0f, -1.0f,  1.0f,  1.0f,
        // Right face
         1.0f,  1.0f,  1.0f,  1.0f, -1.0f, -1.0f,  1.0f,  1.0f, -1.0f,
         1.0f, -1.0f, -1.0f,  1.0f,  1.0f,  1.0f,  1.0f, -1.0f,  1.0f,
        // Bottom face
        -1.0f, -1.0f, -1.0f,  1.0f, -1.0f, -1.0f,  1.0f, -1.0f,  1.0f,
         1.0f, -1.0f,  1.0f, -1.0f, -1.0f,  1.0f, -1.0f, -1.0f, -1.0f,
        // Top face
        -1.0f,  1.0f, -1.0f,  1.0f,  1.0f,  1.0f,  1.0f,  1.0f, -1.0f,
         1.0f,  1.0f,  1.0f, -1.0f,  1.0f, -1.0f, -1.0f,  1.0f,  1.0f,
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

void WorldRenderer::generate_mountain_positions() {
    mountain_positions_.clear();
    
    float world_center_x = world_width_ / 2.0f;
    float world_center_z = world_height_ / 2.0f;
    float ring_radius = 4000.0f;
    
    // EPIC MASSIVE mountains
    for (int ring = 0; ring < 2; ++ring) {
        float current_radius = ring_radius + ring * 3000.0f;
        int num_mountains = 8 + ring * 4;
        
        for (int i = 0; i < num_mountains; ++i) {
            float angle = (i / static_cast<float>(num_mountains)) * 2.0f * 3.14159f;
            float offset = std::sin(angle * 3.0f + ring) * 500.0f;
            float mx = world_center_x + std::cos(angle) * (current_radius + offset);
            float mz = world_center_z + std::sin(angle) * (current_radius + offset);
            
            MountainPosition mp;
            mp.x = mx;
            mp.z = mz;
            mp.rotation = angle * 57.2958f + std::sin(angle * 3.0f) * 45.0f;
            
            float base_scale = 4000.0f + ring * 2000.0f;
            mp.scale = base_scale + std::sin(angle * 4.0f + ring) * 1000.0f;
            mp.y = -mp.scale * 0.3f - 400.0f;
            mp.size_type = 2;
            
            mountain_positions_.push_back(mp);
        }
    }
    
    // TITAN peaks in the far distance
    for (int i = 0; i < 5; ++i) {
        float angle = (i / 5.0f) * 2.0f * 3.14159f + 0.3f;
        
        MountainPosition mp;
        mp.x = world_center_x + std::cos(angle) * 10000.0f;
        mp.z = world_center_z + std::sin(angle) * 10000.0f;
        mp.rotation = angle * 57.2958f + 45.0f;
        mp.scale = 8000.0f + std::sin(angle * 2.0f) * 1600.0f;
        mp.y = -mp.scale * 0.35f - 600.0f;
        mp.size_type = 2;
        
        mountain_positions_.push_back(mp);
    }
}

void WorldRenderer::render_skybox(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd,
                                   const glm::mat4& view, const glm::mat4& projection) {
    if (!skybox_vertex_buffer_ || !pipeline_registry_ || !pass || !cmd) return;
    
    auto* pipeline = pipeline_registry_->get_skybox_pipeline();
    if (!pipeline) return;
    
    pipeline->bind(pass);
    
    // Push vertex uniforms - compute view_projection, removing translation from view
    // (skybox should stay centered on camera)
    SkyboxVertexUniforms vs_uniforms = {};
    glm::mat4 view_no_translation = glm::mat4(glm::mat3(view));
    vs_uniforms.view_projection = projection * view_no_translation;
    SDL_PushGPUVertexUniformData(cmd, 0, &vs_uniforms, sizeof(vs_uniforms));
    
    // Push fragment uniforms - sky colors based on time of day
    SkyboxFragmentUniforms fs_uniforms = {};
    // Use sun_direction to determine sky colors
    float sun_height = sun_direction_.y;
    if (sun_height > 0.0f) {
        // Day sky
        fs_uniforms.sky_color_top = glm::vec3(0.3f, 0.5f, 0.9f);
        fs_uniforms.sky_color_bottom = glm::vec3(0.6f, 0.7f, 0.9f);
    } else {
        // Night sky
        fs_uniforms.sky_color_top = glm::vec3(0.02f, 0.02f, 0.1f);
        fs_uniforms.sky_color_bottom = glm::vec3(0.1f, 0.1f, 0.2f);
    }
    SDL_PushGPUFragmentUniformData(cmd, 0, &fs_uniforms, sizeof(fs_uniforms));
    
    // Bind vertex buffer
    SDL_GPUBufferBinding vb_binding = {};
    vb_binding.buffer = skybox_vertex_buffer_->handle();
    vb_binding.offset = 0;
    SDL_BindGPUVertexBuffers(pass, 0, &vb_binding, 1);
    
    // Draw skybox (36 vertices = 12 triangles = 6 faces)
    SDL_DrawGPUPrimitives(pass, 36, 1, 0, 0);
}

void WorldRenderer::render_mountains(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd,
                                      const glm::mat4& view, const glm::mat4& projection,
                                      const glm::vec3& camera_pos, const glm::vec3& light_dir) {
    if (!model_manager_ || !pipeline_registry_ || !pass || !cmd) return;
    
    Model* mountain_small = model_manager_->get_model("mountain_small");
    Model* mountain_medium = model_manager_->get_model("mountain_medium");
    Model* mountain_large = model_manager_->get_model("mountain_large");
    
    if (!mountain_small && !mountain_medium && !mountain_large) return;
    
    auto* pipeline = pipeline_registry_->get_model_pipeline();
    if (!pipeline) return;
    
    pipeline->bind(pass);
    
    for (const auto& mp : mountain_positions_) {
        Model* mountain = nullptr;
        switch (mp.size_type) {
            case 0: mountain = mountain_small; break;
            case 1: mountain = mountain_medium; break;
            case 2: mountain = mountain_large; break;
        }
        if (!mountain) {
            mountain = mountain_medium ? mountain_medium : (mountain_small ? mountain_small : mountain_large);
        }
        if (!mountain) continue;
        
        glm::mat4 model_mat = glm::mat4(1.0f);
        model_mat = glm::translate(model_mat, glm::vec3(mp.x, mp.y, mp.z));
        model_mat = glm::rotate(model_mat, glm::radians(mp.rotation), glm::vec3(0.0f, 1.0f, 0.0f));
        model_mat = glm::scale(model_mat, glm::vec3(mp.scale));
        
        float cx = (mountain->min_x + mountain->max_x) / 2.0f;
        float cy = mountain->min_y;
        float cz = (mountain->min_z + mountain->max_z) / 2.0f;
        model_mat = model_mat * glm::translate(glm::mat4(1.0f), glm::vec3(-cx, -cy, -cz));
        
        // Set up vertex uniforms (matches model_vertex cbuffer b0)
        ModelVertexUniforms vs_uniforms = {};
        vs_uniforms.model = model_mat;
        vs_uniforms.view = view;
        vs_uniforms.projection = projection;
        vs_uniforms.camera_pos = camera_pos;
        vs_uniforms.light_space_matrix = glm::mat4(1.0f);  // No shadows for mountains
        
        // Set up fragment uniforms (matches model_fragment cbuffer b0)
        ModelFragmentUniforms fs_uniforms = {};
        fs_uniforms.light_dir = light_dir;
        fs_uniforms.ambient = 0.5f;
        fs_uniforms.light_color = glm::vec3(1.0f, 0.95f, 0.9f);
        fs_uniforms.tint_color = glm::vec4(1.0f);
        fs_uniforms.fog_color = glm::vec3(0.55f, 0.55f, 0.6f);
        fs_uniforms.fog_start = 3000.0f;
        fs_uniforms.fog_end = 12000.0f;
        fs_uniforms.shadows_enabled = 0;
        fs_uniforms.fog_enabled = 1;
        
        for (auto& mesh : mountain->meshes) {
            // Only draw meshes that have valid GPU buffers and index data
            if (!mesh.gpu_vertex_buffer || mesh.indices.empty()) {
                continue;
            }
            
            fs_uniforms.has_texture = (mesh.has_texture && mesh.gpu_texture) ? 1 : 0;
            SDL_PushGPUVertexUniformData(cmd, 0, &vs_uniforms, sizeof(vs_uniforms));
            SDL_PushGPUFragmentUniformData(cmd, 0, &fs_uniforms, sizeof(fs_uniforms));
            
            // Bind texture if available
            if (mesh.has_texture && mesh.gpu_texture && sampler_) {
                SDL_GPUTextureSamplerBinding tex_binding = {};
                tex_binding.texture = mesh.gpu_texture->handle();
                tex_binding.sampler = sampler_;
                SDL_BindGPUFragmentSamplers(pass, 0, &tex_binding, 1);
            }
            
            // Bind vertex buffer
            SDL_GPUBufferBinding vb_binding = {};
            vb_binding.buffer = mesh.gpu_vertex_buffer->handle();
            vb_binding.offset = 0;
            SDL_BindGPUVertexBuffers(pass, 0, &vb_binding, 1);
            
            // Bind index buffer and draw
            if (mesh.gpu_index_buffer) {
                SDL_GPUBufferBinding ib_binding = {};
                ib_binding.buffer = mesh.gpu_index_buffer->handle();
                ib_binding.offset = 0;
                SDL_BindGPUIndexBuffer(pass, &ib_binding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
                SDL_DrawGPUIndexedPrimitives(pass, static_cast<uint32_t>(mesh.indices.size()), 1, 0, 0, 0);
            }
        }
    }
}

void WorldRenderer::render_grid(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd,
                                 const glm::mat4& view, const glm::mat4& projection) {
    if (!grid_vertex_buffer_ || !pipeline_registry_ || !pass || !cmd) return;
    
    auto* pipeline = pipeline_registry_->get_grid_pipeline();
    if (!pipeline) return;
    
    pipeline->bind(pass);
    
    // Push uniforms
    GridUniforms uniforms = {};
    uniforms.view_projection = projection * view;
    SDL_PushGPUVertexUniformData(cmd, 0, &uniforms, sizeof(uniforms));
    
    // Bind vertex buffer
    SDL_GPUBufferBinding vb_binding = {};
    vb_binding.buffer = grid_vertex_buffer_->handle();
    vb_binding.offset = 0;
    SDL_BindGPUVertexBuffers(pass, 0, &vb_binding, 1);
    
    // Draw grid lines
    SDL_DrawGPUPrimitives(pass, grid_vertex_count_, 1, 0, 0);
}

// =============================================================================
// Legacy OpenGL render functions (deprecated - for backward compatibility)
// TODO: Remove after renderer.cpp is fully migrated to SDL3 GPU API
// =============================================================================

void WorldRenderer::render_skybox(const glm::mat4& view, const glm::mat4& projection) {
    // Legacy version - no-op without GPU device
    // Callers should be updated to use the SDL3 GPU API version
    (void)view;
    (void)projection;
    // No rendering - needs migration to SDL3 GPU API
}

void WorldRenderer::render_mountains(const glm::mat4& view, const glm::mat4& projection,
                                      const glm::vec3& camera_pos, const glm::vec3& light_dir) {
    // Legacy version - no-op without GPU device
    // Callers should be updated to use the SDL3 GPU API version
    (void)view;
    (void)projection;
    (void)camera_pos;
    (void)light_dir;
    // No rendering - needs migration to SDL3 GPU API
}

void WorldRenderer::render_grid(const glm::mat4& view, const glm::mat4& projection) {
    // Legacy version - no-op without GPU device
    // Callers should be updated to use the SDL3 GPU API version
    (void)view;
    (void)projection;
    // No rendering - needs migration to SDL3 GPU API
}

} // namespace mmo
