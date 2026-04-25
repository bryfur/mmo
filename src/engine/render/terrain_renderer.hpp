#pragma once

#include "engine/gpu/gpu_buffer.hpp"
#include "engine/gpu/gpu_device.hpp"
#include "engine/gpu/gpu_texture.hpp"
#include "engine/gpu/pipeline_registry.hpp"
#include "engine/heightmap.hpp"
#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace mmo::engine::scene {
class Frustum;
}

namespace mmo::engine::render {

namespace gpu = mmo::engine::gpu;

/**
 * Terrain vertex format for SDL3 GPU API
 * Matches the vertex attributes expected by terrain.vert.hlsl
 */
struct TerrainVertex {
    glm::vec3 position; // POSITION
    glm::vec2 texCoord; // TEXCOORD0
    glm::vec4 color;    // COLOR0
    glm::vec3 normal;   // NORMAL
};

// Verify TerrainVertex is tightly packed as expected by pipeline_registry
static_assert(sizeof(TerrainVertex) == sizeof(float) * 12,
              "TerrainVertex must be 12 floats (48 bytes) to match pipeline vertex stride");

/**
 * Terrain transform uniforms - matches terrain.vert.hlsl cbuffer
 */
struct alignas(16) TerrainTransformUniforms {
    glm::mat4 view;
    glm::mat4 projection;
    glm::vec3 cameraPos;
    float _padding0;
};

/**
 * Terrain lighting uniforms - matches terrain.frag.hlsl cbuffer
 */
struct alignas(16) TerrainLightingUniforms {
    glm::vec3 fogColor;    // offset 0
    float fogStart;        // offset 12
    float fogEnd;          // offset 16
    float world_size;      // offset 20
    float _padding0[2];    // offset 24 — pad to 32 so lightDir aligns to 16-byte boundary
    glm::vec3 lightDir;    // offset 32
    float _padding1;       // offset 44
    float ambientStrength; // offset 48
    float sunIntensity;    // offset 52
    float _padding2[2];    // offset 56 — pad to 64 (16-byte aligned struct end)
};

/**
 * TerrainRenderer handles terrain rendering using server-provided heightmaps.
 * Ported to SDL3 GPU API - replaces OpenGL calls with GPUBuffer and GPUTexture.
 */
class TerrainRenderer {
public:
    TerrainRenderer();
    ~TerrainRenderer();

    // Non-copyable
    TerrainRenderer(const TerrainRenderer&) = delete;
    TerrainRenderer& operator=(const TerrainRenderer&) = delete;

    /**
     * Initialize terrain resources.
     * @param device The GPU device for resource creation
     * @param pipeline_registry The pipeline registry for getting terrain pipeline
     * @param world_width World X dimension
     * @param world_height World Z dimension
     */
    bool init(gpu::GPUDevice& device, gpu::PipelineRegistry& pipeline_registry, float world_width, float world_height);

    /**
     * Set heightmap from server data. Uploads to GPU texture.
     */
    void set_heightmap(const engine::Heightmap& heightmap);

    /**
     * Update splatmap texture from CPU data.
     * @param data RGBA pixel data (must be splatmap_resolution^2 * 4 bytes)
     * @param resolution Splatmap resolution (width and height)
     */
    void update_splatmap(const uint8_t* data, uint32_t resolution);

    /**
     * Clean up terrain resources.
     */
    void shutdown();

    /**
     * Render the terrain mesh using SDL3 GPU API.
     * @param pass The render pass to draw into
     * @param cmd The command buffer for push constants
     * @param view View matrix
     * @param projection Projection matrix
     * @param camera_pos Camera position
     * @param light_dir Light direction
     */
    void render(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd, const glm::mat4& view, const glm::mat4& projection,
                const glm::vec3& camera_pos, const glm::vec3& light_dir,
                const SDL_GPUTextureSamplerBinding* shadow_bindings = nullptr, int shadow_binding_count = 0,
                const scene::Frustum* frustum = nullptr);

    // Per-frame cluster lighting plumbing. Buffers and uniform are owned by
    // the SceneRenderer's ClusterGrid; nullptr disables clustered lighting.
    void set_cluster_lighting(SDL_GPUBuffer* light_data, SDL_GPUBuffer* cluster_offsets, SDL_GPUBuffer* light_indices,
                              const void* params, size_t params_size);

    /**
     * Render terrain into a shadow depth map.
     */
    void render_shadow(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd, const glm::mat4& light_view_projection,
                       const scene::Frustum* frustum = nullptr);

    /**
     * Get terrain height at any world position.
     * Samples from CPU-side heightmap data (for physics, placement, etc.)
     */
    float get_height(float x, float z) const;

    /**
     * Get terrain normal at any world position.
     */
    glm::vec3 get_normal(float x, float z) const;

    // Accessors
    float world_width() const { return world_width_; }
    float world_height() const { return world_height_; }
    bool has_heightmap() const { return heightmap_ != nullptr; }
    gpu::GPUTexture* heightmap_texture() { return heightmap_texture_.get(); }
    gpu::GPUTexture* material_array_texture() { return material_array_texture_.get(); }

    // Settings
    void set_fog_color(const glm::vec3& color) { fog_color_ = color; }
    void set_fog_range(float start, float end) {
        fog_start_ = start;
        fog_end_ = end;
    }
    void set_lighting_tunables(float ambient_strength, float sun_intensity) {
        ambient_strength_ = ambient_strength;
        sun_intensity_ = sun_intensity;
    }

    /**
     * Set anisotropic filter level for terrain textures.
     * Recreates the grass sampler with the new anisotropy level.
     */
    void set_anisotropic_filter(float level);

private:
    void generate_terrain_mesh();
    void generate_shadow_terrain_mesh(); // low-LOD mesh for shadow passes
    void load_terrain_textures();
    void upload_heightmap_texture();

    gpu::GPUDevice* device_ = nullptr;
    gpu::PipelineRegistry* pipeline_registry_ = nullptr;

    SDL_GPUBuffer* cluster_light_data_ = nullptr;
    SDL_GPUBuffer* cluster_offsets_ = nullptr;
    SDL_GPUBuffer* cluster_indices_ = nullptr;
    const void* cluster_params_ = nullptr;
    size_t cluster_params_size_ = 0;

    float world_width_ = 0.0f;
    float world_height_ = 0.0f;

    // Server-provided heightmap (CPU side for sampling)
    std::unique_ptr<engine::Heightmap> heightmap_;

    // GPU resources
    std::unique_ptr<gpu::GPUTexture> heightmap_texture_;

    // Terrain material textures (using Texture2DArray to reduce sampler count)
    std::unique_ptr<gpu::GPUTexture> material_array_texture_; // 4 layers: grass, dirt, rock, sand
    std::unique_ptr<gpu::GPUTexture> splatmap_texture_;

    std::unique_ptr<gpu::GPUSampler> material_sampler_;
    std::unique_ptr<gpu::GPUBuffer> vertex_buffer_;
    std::unique_ptr<gpu::GPUBuffer> index_buffer_;
    uint32_t index_count_ = 0;

    // Low-LOD mesh used only for shadow passes. Coarser cell size -> ~16x fewer triangles.
    // Shadow resolution already smooths over the mesh detail, so visual impact is negligible.
    std::unique_ptr<gpu::GPUBuffer> shadow_vertex_buffer_;
    std::unique_ptr<gpu::GPUBuffer> shadow_index_buffer_;
    uint32_t shadow_index_count_ = 0;

    // Terrain tiling: each tile is a rectangular region of cells with its own
    // index range and world-space AABB. Per-frame frustum culling skips tiles
    // outside the view frustum — typically 80–95% are culled per pass.
    struct TerrainTile {
        uint32_t first_index = 0;
        uint32_t index_count = 0;
        glm::vec3 aabb_min = glm::vec3(0.0f);
        glm::vec3 aabb_max = glm::vec3(0.0f);
    };
    std::vector<TerrainTile> tiles_;
    std::vector<TerrainTile> shadow_tiles_;

    // Fog settings
    glm::vec3 fog_color_ = glm::vec3(0.35f, 0.45f, 0.6f);
    float fog_start_ = 1600.0f;
    float fog_end_ = 12000.0f;

    // Lighting tunables (per-frame from GraphicsSettings)
    float ambient_strength_ = 0.5f;
    float sun_intensity_ = 1.0f;
};

} // namespace mmo::engine::render
