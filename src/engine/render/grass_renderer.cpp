#include "grass_renderer.hpp"
#include "SDL3/SDL_gpu.h"
#include "engine/gpu/gpu_buffer.hpp"
#include "engine/gpu/gpu_device.hpp"
#include "engine/gpu/gpu_texture.hpp"
#include "engine/gpu/gpu_types.hpp"
#include "engine/gpu/pipeline_registry.hpp"
#include "engine/scene/frustum.hpp"
#include <SDL3/SDL_log.h>
#include <algorithm>
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

    SDL_Log("Initializing grass renderer (chunked instanced, AAA-quality)...");

    device_ = &device;
    pipeline_registry_ = &pipeline_registry;
    world_width_ = world_width;
    world_height_ = world_height;

    // Heightmap sampler: linear filtering for smooth terrain-height transitions.
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

    // Pre-size chunk storage buffer for a reasonable upper bound of visible chunks.
    // At view_dist=600 and chunk=32, (600/32)^2*pi ≈ 1100 candidate chunks;
    // usually 200-400 visible after frustum cull.
    constexpr size_t initial_capacity_chunks = 2048;
    chunk_storage_capacity_bytes_ = initial_capacity_chunks * sizeof(glm::vec4);
    chunk_storage_buffer_ = gpu::GPUBuffer::create_dynamic(
        device, gpu::GPUBuffer::Type::Storage, chunk_storage_capacity_bytes_);
    if (!chunk_storage_buffer_) {
        SDL_Log("GrassRenderer::init: Failed to create chunk storage buffer");
        return false;
    }
    visible_chunks_scratch_.reserve(initial_capacity_chunks);

    initialized_ = true;
    SDL_Log("Grass renderer initialized (blade mesh: %u indices, chunks: %ux%u, blades/chunk: %u)",
            blade_index_count_, BLADES_PER_CHUNK_SIDE, BLADES_PER_CHUNK_SIDE, BLADES_PER_CHUNK_SQ);
    return true;
}

void GrassRenderer::update(float delta_time, float current_time) {
    (void)delta_time;
    current_time_ = current_time;
}

void GrassRenderer::generate_blade_mesh() {
    // Unit blade: base at Y=0, tip at Y=1, width tapers toward tip.
    // 4 segments = 5 rows of 2 vertices = 10 vertices, 8 triangles.
    const int segments = 4;
    const float base_width = 0.5f;

    std::vector<gpu::Vertex3D> vertices;
    std::vector<uint32_t> indices;

    // Base-to-tip color gradient
    glm::vec4 base_color(0.08f, 0.18f, 0.04f, 1.0f);
    glm::vec4 tip_color(0.25f, 0.55f, 0.12f, 1.0f);

    for (int i = 0; i <= segments; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(segments);
        float w = base_width * (1.0f - t * 0.9f);
        float y = t;

        glm::vec3 normal(0.0f, 0.0f, 1.0f);
        glm::vec4 color = glm::mix(base_color, tip_color, t);

        gpu::Vertex3D left;
        left.position = glm::vec3(-w, y, 0.0f);
        left.normal = normal;
        left.texcoord = glm::vec2(0.0f, t);
        left.color = color;
        vertices.push_back(left);

        gpu::Vertex3D right;
        right.position = glm::vec3(w, y, 0.0f);
        right.normal = normal;
        right.texcoord = glm::vec2(1.0f, t);
        right.color = color;
        vertices.push_back(right);
    }

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

void GrassRenderer::upload_chunks(SDL_GPUCommandBuffer* cmd, const glm::vec3& camera_pos,
                                  const scene::Frustum* frustum) {
    if (!initialized_ || !cmd) return;

    // === CPU chunk culling ===
    const float chunk_size = CHUNK_SIZE;
    const float view_dist = grass_view_distance;
    const float view_dist_sq = view_dist * view_dist;

    const float cam_chunk_x = std::floor(camera_pos.x / chunk_size) * chunk_size;
    const float cam_chunk_z = std::floor(camera_pos.z / chunk_size) * chunk_size;
    const int half_extent = static_cast<int>(std::ceil(view_dist / chunk_size)) + 1;

    const float y_min = heightmap_params_.min_height;
    const float y_max = heightmap_params_.max_height + 20.0f;  // + blade height upper bound

    visible_chunks_scratch_.clear();

    for (int iz = -half_extent; iz <= half_extent; ++iz) {
        for (int ix = -half_extent; ix <= half_extent; ++ix) {
            float ox = cam_chunk_x + static_cast<float>(ix) * chunk_size;
            float oz = cam_chunk_z + static_cast<float>(iz) * chunk_size;
            float cx = ox + chunk_size * 0.5f;
            float cz = oz + chunk_size * 0.5f;

            float dx = cx - camera_pos.x;
            float dz = cz - camera_pos.z;
            float dist_sq = dx * dx + dz * dz;
            if (dist_sq > view_dist_sq) continue;

            if (ox + chunk_size < 50.0f || ox > world_width_ - 50.0f ||
                oz + chunk_size < 50.0f || oz > world_height_ - 50.0f) continue;

            float town_cx = world_width_ * 0.5f;
            float town_cz = world_height_ * 0.5f;
            if (std::abs(cx - town_cx) < 200.0f && std::abs(cz - town_cz) < 200.0f) continue;

            if (frustum) {
                glm::vec3 aabb_min(ox, y_min, oz);
                glm::vec3 aabb_max(ox + chunk_size, y_max, oz + chunk_size);
                if (!frustum->intersects_aabb(aabb_min, aabb_max)) continue;
            }

            visible_chunks_scratch_.emplace_back(ox, 0.0f, oz, 0.0f);
        }
    }

    if (visible_chunks_scratch_.empty()) return;

    // Grow storage buffer if needed (copy pass internally — must be outside render pass).
    size_t needed_bytes = visible_chunks_scratch_.size() * sizeof(glm::vec4);
    if (needed_bytes > chunk_storage_capacity_bytes_) {
        chunk_storage_capacity_bytes_ = needed_bytes * 2;
        chunk_storage_buffer_ = gpu::GPUBuffer::create_dynamic(
            *device_, gpu::GPUBuffer::Type::Storage, chunk_storage_capacity_bytes_);
        if (!chunk_storage_buffer_) {
            SDL_Log("GrassRenderer::upload_chunks: Failed to grow chunk storage buffer");
            return;
        }
    }

    chunk_storage_buffer_->update(cmd, visible_chunks_scratch_.data(), needed_bytes);
}

void GrassRenderer::render(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd,
                           const glm::mat4& view, const glm::mat4& projection,
                           const glm::vec3& camera_pos, const glm::vec3& light_dir,
                           const SDL_GPUTextureSamplerBinding* shadow_bindings,
                           int shadow_binding_count) {
    if (!initialized_ || !pipeline_registry_ || !pass || !cmd) return;
    if (!blade_vertex_buffer_ || !blade_index_buffer_ || blade_index_count_ == 0) return;
    if (!heightmap_texture_ || !heightmap_sampler_) return;
    if (visible_chunks_scratch_.empty() || !chunk_storage_buffer_) return;

    auto* pipeline = pipeline_registry_->get_grass_pipeline();
    if (!pipeline) {
        SDL_Log("GrassRenderer::render: Failed to get grass pipeline");
        return;
    }

    const float chunk_size = CHUNK_SIZE;
    const float blade_spacing = chunk_size / static_cast<float>(BLADES_PER_CHUNK_SIDE);
    const float view_dist = grass_view_distance;

    // === Draw ===
    pipeline->bind(pass);

    // Vertex uniforms.
    GrassVertexUniforms vu{};
    vu.view_projection = projection * view;
    vu.camera_pos = camera_pos;
    vu.time = current_time_;
    vu.grass_view_distance = view_dist;
    vu.wind_direction = glm::vec2(1.0f, 0.3f);
    vu.wind_strength = wind_magnitude;
    vu.chunk_size = chunk_size;
    vu.blade_spacing = blade_spacing;
    vu.blades_per_chunk_side = BLADES_PER_CHUNK_SIDE;
    vu.blades_per_chunk_sq = BLADES_PER_CHUNK_SQ;
    vu.heightmap_origin_x = heightmap_params_.world_origin_x;
    vu.heightmap_origin_z = heightmap_params_.world_origin_z;
    vu.heightmap_world_size = heightmap_params_.world_size;
    vu.heightmap_min_height = heightmap_params_.min_height;
    vu.heightmap_max_height = heightmap_params_.max_height;

    SDL_PushGPUVertexUniformData(cmd, 0, &vu, sizeof(vu));

    // Fragment uniforms (fog + lighting).
    GrassLightingUniforms fu{};
    fu.camera_pos = camera_pos;
    fu.fog_start = view_dist * 0.6f;
    fu.fog_color = glm::vec3(0.12f, 0.14f, 0.2f);
    fu.fog_end = view_dist;
    fu.fog_enabled = 1;
    fu.light_dir = light_dir;
    fu.ambient_strength = ambient_strength;
    fu.sun_intensity = sun_intensity;
    SDL_PushGPUFragmentUniformData(cmd, 0, &fu, sizeof(fu));

    // Vertex sampler slot 0: heightmap.
    SDL_GPUTextureSamplerBinding heightmap_binding = {
        heightmap_texture_->handle(),
        heightmap_sampler_->handle()
    };
    SDL_BindGPUVertexSamplers(pass, 0, &heightmap_binding, 1);

    // Vertex storage buffer slot 0: visible-chunk origins.
    SDL_GPUBuffer* chunk_buf = chunk_storage_buffer_->handle();
    SDL_BindGPUVertexStorageBuffers(pass, 0, &chunk_buf, 1);

    // Fragment samplers slots 0-3: shadow cascades.
    if (shadow_bindings && shadow_binding_count > 0) {
        SDL_BindGPUFragmentSamplers(pass, 0, shadow_bindings, shadow_binding_count);
    }

    if (cluster_light_data_ && cluster_offsets_ && cluster_indices_ && cluster_params_) {
        SDL_GPUBuffer* bufs[3] = { cluster_light_data_, cluster_offsets_, cluster_indices_ };
        SDL_BindGPUFragmentStorageBuffers(pass, 0, bufs, 3);
        SDL_PushGPUFragmentUniformData(cmd, 2, cluster_params_, static_cast<Uint32>(cluster_params_size_));
    }

    // Blade mesh.
    SDL_GPUBufferBinding vb_binding = { blade_vertex_buffer_->handle(), 0 };
    SDL_BindGPUVertexBuffers(pass, 0, &vb_binding, 1);

    SDL_GPUBufferBinding ib_binding = { blade_index_buffer_->handle(), 0 };
    SDL_BindGPUIndexBuffer(pass, &ib_binding, SDL_GPU_INDEXELEMENTSIZE_32BIT);

    uint32_t instance_count =
        static_cast<uint32_t>(visible_chunks_scratch_.size()) * BLADES_PER_CHUNK_SQ;
    SDL_DrawGPUIndexedPrimitives(pass, blade_index_count_, instance_count, 0, 0, 0);
}

void GrassRenderer::shutdown() {
    blade_vertex_buffer_.reset();
    blade_index_buffer_.reset();
    chunk_storage_buffer_.reset();
    chunk_storage_capacity_bytes_ = 0;
    visible_chunks_scratch_.clear();
    heightmap_sampler_.reset();
    heightmap_texture_ = nullptr;
    device_ = nullptr;
    pipeline_registry_ = nullptr;
    blade_index_count_ = 0;
    initialized_ = false;
}

} // namespace mmo::engine::render
