#pragma once

#include "../gpu/gpu_device.hpp"
#include "../gpu/gpu_buffer.hpp"
#include "../gpu/gpu_texture.hpp"
#include "../gpu/pipeline_registry.hpp"
#include <glm/glm.hpp>
#include <memory>

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
 * Grass vertex uniforms - matches grass.vert.hlsl cbuffer
 */
struct alignas(16) GrassVertexUniforms {
    glm::mat4 view_projection;       // 64 bytes
    glm::vec3 camera_grid;           // 12 bytes - snapped camera position
    float time;                      // 4 bytes
    float wind_strength;             // 4 bytes
    float grass_spacing;             // 4 bytes
    float grass_view_distance;       // 4 bytes
    int grid_radius;                 // 4 bytes
    glm::vec2 wind_direction;        // 8 bytes
    float heightmap_world_origin_x;  // 4 bytes
    float heightmap_world_origin_z;  // 4 bytes
    float heightmap_world_size;      // 4 bytes
    float heightmap_min_height;      // 4 bytes
    float heightmap_max_height;      // 4 bytes
    float world_width;               // 4 bytes
    float world_height;              // 4 bytes
    glm::vec2 camera_forward;        // 8 bytes (XZ direction camera is looking)
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
    int32_t _padding[3];
};

/**
 * GPU-based grass renderer (SDL3 GPU API)
 *
 * Uses fully GPU-driven instanced rendering. A single blade mesh is created
 * at init time. The vertex shader derives per-instance position from
 * SV_InstanceID, samples a heightmap texture for terrain height, and applies
 * wind animation. No per-frame CPU work beyond setting uniforms.
 */
class GrassRenderer {
public:
    GrassRenderer();
    ~GrassRenderer();

    GrassRenderer(const GrassRenderer&) = delete;
    GrassRenderer& operator=(const GrassRenderer&) = delete;

    bool init(gpu::GPUDevice& device, gpu::PipelineRegistry& pipeline_registry,
              float world_width, float world_height);

    void update(float delta_time, float current_time);

    void render(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd,
                const glm::mat4& view, const glm::mat4& projection,
                const glm::vec3& camera_pos, const glm::vec3& light_dir);

    void shutdown();

    void set_heightmap(gpu::GPUTexture* texture, const HeightmapParams& params) {
        heightmap_texture_ = texture;
        heightmap_params_ = params;
    }

    // Wind parameters
    float wind_magnitude = 0.8f;

    // Grass parameters
    float grass_spacing = 8.0f;
    float grass_view_distance = 2000.0f;

private:
    void generate_blade_mesh();

    gpu::GPUDevice* device_ = nullptr;
    gpu::PipelineRegistry* pipeline_registry_ = nullptr;

    // Single blade mesh (created once at init)
    std::unique_ptr<gpu::GPUBuffer> blade_vertex_buffer_;
    std::unique_ptr<gpu::GPUBuffer> blade_index_buffer_;
    uint32_t blade_index_count_ = 0;

    // Heightmap for GPU height sampling
    gpu::GPUTexture* heightmap_texture_ = nullptr;
    std::unique_ptr<gpu::GPUSampler> heightmap_sampler_;
    HeightmapParams heightmap_params_;

    float world_width_ = 0.0f;
    float world_height_ = 0.0f;
    float current_time_ = 0.0f;
    bool initialized_ = false;
};

} // namespace mmo::engine::render
