#include "grass_renderer.hpp"
#include "SDL3/SDL_gpu.h"
#include "engine/gpu/gpu_buffer.hpp"
#include "engine/gpu/gpu_device.hpp"
#include "engine/gpu/gpu_texture.hpp"
#include "engine/gpu/gpu_types.hpp"
#include "engine/gpu/pipeline_registry.hpp"
#include <SDL3/SDL_log.h>
#include <cmath>
#include <cstdint>
#include <vector>

namespace mmo::engine::render {

namespace gpu = mmo::engine::gpu;

GrassRenderer::GrassRenderer() = default;

GrassRenderer::~GrassRenderer() {
    shutdown();
}

bool GrassRenderer::init(gpu::GPUDevice& device, gpu::PipelineRegistry& pipeline_registry,
                         float world_width, float world_height) {
    if (initialized_) return true;

    SDL_Log("Initializing grass renderer (GPU instanced)...");

    device_ = &device;
    pipeline_registry_ = &pipeline_registry;
    world_width_ = world_width;
    world_height_ = world_height;

    // Create sampler for heightmap texture (clamp to edge, linear filtering)
    heightmap_sampler_ = gpu::GPUSampler::create(device, gpu::SamplerConfig::linear_clamp());
    if (!heightmap_sampler_) {
        SDL_Log("GrassRenderer::init: Failed to create heightmap sampler");
        return false;
    }

    generate_blade_mesh();

    if (!blade_vertex_buffer_ || !blade_index_buffer_) {
        SDL_Log("GrassRenderer::init: Failed to create blade mesh");
        return false;
    }

    initialized_ = true;
    SDL_Log("Grass renderer initialized (blade mesh: %u indices)", blade_index_count_);
    return true;
}

void GrassRenderer::update(float delta_time, float current_time) {
    current_time_ = current_time;
}

void GrassRenderer::generate_blade_mesh() {
    // Create a single unit grass blade: base at Y=0, tip at Y=1
    // Width tapers from base to tip, centered on X axis
    // 4 segments = 5 rows of 2 vertices = 10 vertices

    const int segments = 4;
    const float base_width = 0.5f;

    std::vector<gpu::Vertex3D> vertices;
    std::vector<uint32_t> indices;

    // Base-to-tip color gradient
    glm::vec4 base_color(0.08f, 0.18f, 0.04f, 1.0f);
    glm::vec4 tip_color(0.25f, 0.55f, 0.12f, 1.0f);

    for (int i = 0; i <= segments; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(segments);
        float w = base_width * (1.0f - t * 0.9f); // taper toward tip
        float y = t;

        glm::vec3 normal(0.0f, 0.0f, 1.0f); // face +Z, shader will rotate
        glm::vec4 color = glm::mix(base_color, tip_color, t);

        // Left vertex
        gpu::Vertex3D left;
        left.position = glm::vec3(-w, y, 0.0f);
        left.normal = normal;
        left.texcoord = glm::vec2(0.0f, t);
        left.color = color;
        vertices.push_back(left);

        // Right vertex
        gpu::Vertex3D right;
        right.position = glm::vec3(w, y, 0.0f);
        right.normal = normal;
        right.texcoord = glm::vec2(1.0f, t);
        right.color = color;
        vertices.push_back(right);
    }

    // Triangle indices for quad strip
    for (int i = 0; i < segments; ++i) {
        uint32_t base = i * 2;
        indices.push_back(base);
        indices.push_back(base + 2);
        indices.push_back(base + 1);
        indices.push_back(base + 1);
        indices.push_back(base + 2);
        indices.push_back(base + 3);
    }

    blade_vertex_buffer_ = gpu::GPUBuffer::create_static(
        *device_, gpu::GPUBuffer::Type::Vertex,
        vertices.data(), vertices.size() * sizeof(gpu::Vertex3D));

    blade_index_buffer_ = gpu::GPUBuffer::create_static(
        *device_, gpu::GPUBuffer::Type::Index,
        indices.data(), indices.size() * sizeof(uint32_t));

    blade_index_count_ = static_cast<uint32_t>(indices.size());
}

void GrassRenderer::render(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd,
                           const glm::mat4& view, const glm::mat4& projection,
                           const glm::vec3& camera_pos, const glm::vec3& light_dir,
                           const SDL_GPUTextureSamplerBinding* shadow_bindings,
                           int shadow_binding_count) {
    if (!initialized_ || !pipeline_registry_ || !pass || !cmd) return;
    if (!blade_vertex_buffer_ || !blade_index_buffer_ || blade_index_count_ == 0) return;
    if (!heightmap_texture_ || !heightmap_sampler_) return;

    auto* pipeline = pipeline_registry_->get_grass_pipeline();
    if (!pipeline) {
        SDL_Log("GrassRenderer::render: Failed to get grass pipeline");
        return;
    }

    pipeline->bind(pass);

    // Compute grid parameters
    int grid_radius = static_cast<int>(grass_view_distance / grass_spacing);
    int grid_width = 2 * grid_radius + 1;
    uint32_t instance_count = static_cast<uint32_t>(grid_width * grid_width);

    // Snap camera to grid
    glm::vec2 camera_grid = glm::floor(glm::vec2(camera_pos.x, camera_pos.z) / grass_spacing) * grass_spacing;

    // Vertex uniforms
    GrassVertexUniforms vu{};
    vu.view_projection = projection * view;
    vu.camera_grid = glm::vec3(camera_grid.x, 0.0f, camera_grid.y);
    vu.time = current_time_;
    vu.wind_strength = wind_magnitude;
    vu.grass_spacing = grass_spacing;
    vu.grass_view_distance = grass_view_distance;
    vu.grid_radius = grid_radius;
    vu.wind_direction = glm::vec2(1.0f, 0.3f);
    vu.heightmap_world_origin_x = heightmap_params_.world_origin_x;
    vu.heightmap_world_origin_z = heightmap_params_.world_origin_z;
    vu.heightmap_world_size = heightmap_params_.world_size;
    vu.heightmap_min_height = heightmap_params_.min_height;
    vu.heightmap_max_height = heightmap_params_.max_height;
    vu.world_width = world_width_;
    vu.world_height = world_height_;
    // Extract camera forward from view matrix (negative Z row in view space)
    glm::vec3 fwd = -glm::vec3(view[0][2], view[1][2], view[2][2]);
    vu.camera_forward = glm::normalize(glm::vec2(fwd.x, fwd.z));

    SDL_PushGPUVertexUniformData(cmd, 0, &vu, sizeof(vu));

    // Fragment uniforms (fog)
    GrassLightingUniforms fu{};
    fu.camera_pos = camera_pos;
    fu.fog_start = 1200.0f;
    fu.fog_color = glm::vec3(0.12f, 0.14f, 0.2f);
    fu.fog_end = grass_view_distance;
    fu.fog_enabled = 1;

    SDL_PushGPUFragmentUniformData(cmd, 0, &fu, sizeof(fu));

    // Bind heightmap texture+sampler to vertex stage
    SDL_GPUTextureSamplerBinding heightmap_binding = {
        heightmap_texture_->handle(),
        heightmap_sampler_->handle()
    };
    SDL_BindGPUVertexSamplers(pass, 0, &heightmap_binding, 1);

    // Bind shadow cascade textures (slots 0-3)
    if (shadow_bindings && shadow_binding_count > 0) {
        SDL_BindGPUFragmentSamplers(pass, 0, shadow_bindings, shadow_binding_count);
    }

    // Bind blade mesh
    SDL_GPUBufferBinding vb_binding = { blade_vertex_buffer_->handle(), 0 };
    SDL_BindGPUVertexBuffers(pass, 0, &vb_binding, 1);

    SDL_GPUBufferBinding ib_binding = { blade_index_buffer_->handle(), 0 };
    SDL_BindGPUIndexBuffer(pass, &ib_binding, SDL_GPU_INDEXELEMENTSIZE_32BIT);

    // Draw all instances
    SDL_DrawGPUIndexedPrimitives(pass, blade_index_count_, instance_count, 0, 0, 0);
}

void GrassRenderer::shutdown() {
    blade_vertex_buffer_.reset();
    blade_index_buffer_.reset();
    heightmap_sampler_.reset();
    heightmap_texture_ = nullptr;
    device_ = nullptr;
    pipeline_registry_ = nullptr;
    blade_index_count_ = 0;
    initialized_ = false;
}

} // namespace mmo::engine::render
