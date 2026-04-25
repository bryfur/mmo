#pragma once

#include "../gpu/gpu_buffer.hpp"
#include "../gpu/gpu_device.hpp"
#include "../gpu/gpu_texture.hpp"
#include "../gpu/pipeline_registry.hpp"
#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace mmo::engine::scene {
class Frustum;
}

namespace mmo::engine::render {

namespace gpu = mmo::engine::gpu;

/**
 * Heightmap parameters for GPU-side height sampling
 */
struct HeightmapParams {
    float world_origin_x = 0.0f;
    float world_origin_z = 0.0f;
    float world_size = 1.0f;
    float min_height = -500.0f;
    float max_height = 500.0f;
};

/**
 * Grass vertex uniforms - matches grass.vert.hlsl (new chunked layout).
 * Layout is std140-compatible; padding makes vec3 members 16-byte aligned.
 */
struct alignas(16) GrassVertexUniforms {
    glm::mat4 view_projection;      // 64 bytes
    glm::vec3 camera_pos;           // 12 bytes
    float time;                     // 4  (finishes vec4 slot)
    glm::vec3 _pad0;                // 12
    float grass_view_distance;      // 4
    glm::vec2 wind_direction;       // 8
    float wind_strength;            // 4
    float chunk_size;               // 4  (vec4 boundary)
    float blade_spacing;            // 4
    uint32_t blades_per_chunk_side; // 4
    uint32_t blades_per_chunk_sq;   // 4
    float _pad1;                    // 4  (vec4 boundary)
    float heightmap_origin_x;       // 4
    float heightmap_origin_z;       // 4
    float heightmap_world_size;     // 4
    float heightmap_min_height;     // 4  (vec4 boundary)
    float heightmap_max_height;     // 4
    glm::vec3 _pad2;                // 12 (vec4 boundary)
};

/**
 * Grass fragment uniforms - matches grass.frag.hlsl cbuffer
 */
struct alignas(16) GrassLightingUniforms {
    glm::vec3 camera_pos;
    float fog_start;
    glm::vec3 fog_color;
    float fog_end;
    int32_t fog_enabled;
    int32_t _padding0;
    int32_t _padding1;
    int32_t _padding2;
    glm::vec3 light_dir;
    float _padding3;
    float ambient_strength;
    float sun_intensity;
    float _padding4;
    float _padding5;
};

/**
 * GPU chunked grass renderer (SDL3 GPU API)
 *
 * Architecture:
 *  - World is partitioned into fixed-size grass chunks (e.g. 32x32 world units).
 *  - Each chunk contains BLADES_PER_CHUNK_SIDE^2 blades.
 *  - Each frame, CPU frustum-culls chunks against camera and uploads visible
 *    chunk origins into a storage buffer. Shader reads chunk origin via
 *    instance_id / blades_per_chunk_sq.
 *  - Smooth LOD fade replaces discrete density bands → no phase-in pop.
 */
class GrassRenderer {
public:
    GrassRenderer();
    ~GrassRenderer();

    GrassRenderer(const GrassRenderer&) = delete;
    GrassRenderer& operator=(const GrassRenderer&) = delete;

    bool init(gpu::GPUDevice& device, gpu::PipelineRegistry& pipeline_registry, float world_width, float world_height);

    void update(float delta_time, float current_time);

    /// Cull chunks and upload the visible chunk storage buffer.
    /// MUST be called outside any render pass (performs a copy pass internally).
    /// Call this in the pre-pass upload phase, same stage as instance buffers.
    void upload_chunks(SDL_GPUCommandBuffer* cmd, const glm::vec3& camera_pos, const scene::Frustum* frustum);

    /// Draw the grass. Requires upload_chunks() to have been called this frame.
    /// Must be called inside an active render pass.
    void render(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd, const glm::mat4& view, const glm::mat4& projection,
                const glm::vec3& camera_pos, const glm::vec3& light_dir,
                const SDL_GPUTextureSamplerBinding* shadow_bindings = nullptr, int shadow_binding_count = 0);

    void shutdown();

    void set_heightmap(gpu::GPUTexture* texture, const HeightmapParams& params) {
        heightmap_texture_ = texture;
        heightmap_params_ = params;
    }

    void set_cluster_lighting(SDL_GPUBuffer* light_data, SDL_GPUBuffer* cluster_offsets, SDL_GPUBuffer* light_indices,
                              const void* params, size_t params_size) {
        cluster_light_data_ = light_data;
        cluster_offsets_ = cluster_offsets;
        cluster_indices_ = light_indices;
        cluster_params_ = params;
        cluster_params_size_ = params_size;
    }

    // Wind parameters
    float wind_magnitude = 0.8f;

    // Grass parameters
    float grass_view_distance = 600.0f;

    // Lighting tunables (fed from GraphicsSettings each frame)
    float ambient_strength = 0.5f;
    float sun_intensity = 1.0f;

private:
    void generate_blade_mesh();

    // Chunk geometry constants
    static constexpr float CHUNK_SIZE = 32.0f;           // world units per chunk side
    static constexpr uint32_t BLADES_PER_CHUNK_SIDE = 8; // blades per chunk side
    static constexpr uint32_t BLADES_PER_CHUNK_SQ = BLADES_PER_CHUNK_SIDE * BLADES_PER_CHUNK_SIDE;

    gpu::GPUDevice* device_ = nullptr;
    gpu::PipelineRegistry* pipeline_registry_ = nullptr;

    // Single blade mesh (created once at init)
    std::unique_ptr<gpu::GPUBuffer> blade_vertex_buffer_;
    std::unique_ptr<gpu::GPUBuffer> blade_index_buffer_;
    uint32_t blade_index_count_ = 0;

    // Per-frame chunk storage buffer. Reused + grown via capacity-doubling.
    std::unique_ptr<gpu::GPUBuffer> chunk_storage_buffer_;
    size_t chunk_storage_capacity_bytes_ = 0;
    std::vector<glm::vec4> visible_chunks_scratch_; // reused per frame

    // Heightmap for GPU height sampling
    gpu::GPUTexture* heightmap_texture_ = nullptr;
    std::unique_ptr<gpu::GPUSampler> heightmap_sampler_;
    HeightmapParams heightmap_params_;

    float world_width_ = 0.0f;
    float world_height_ = 0.0f;
    float current_time_ = 0.0f;
    bool initialized_ = false;

    SDL_GPUBuffer* cluster_light_data_ = nullptr;
    SDL_GPUBuffer* cluster_offsets_ = nullptr;
    SDL_GPUBuffer* cluster_indices_ = nullptr;
    const void* cluster_params_ = nullptr;
    size_t cluster_params_size_ = 0;
};

} // namespace mmo::engine::render
