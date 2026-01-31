#pragma once

#include "engine/heightmap.hpp"
#include "engine/gpu/gpu_device.hpp"
#include "engine/gpu/gpu_buffer.hpp"
#include "engine/gpu/gpu_texture.hpp"
#include "engine/gpu/pipeline_registry.hpp"
#include <glm/glm.hpp>
#include <memory>

namespace mmo::engine::render {

namespace gpu = mmo::engine::gpu;

/**
 * Terrain vertex format for SDL3 GPU API
 * Matches the vertex attributes expected by terrain.vert.hlsl
 */
struct TerrainVertex {
    glm::vec3 position;  // POSITION
    glm::vec2 texCoord;  // TEXCOORD0
    glm::vec4 color;     // COLOR0
};

// Verify TerrainVertex is tightly packed as expected by pipeline_registry
static_assert(sizeof(TerrainVertex) == sizeof(float) * 9,
    "TerrainVertex must be 9 floats (36 bytes) to match pipeline vertex stride");

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
    float _padding0[3];    // offset 20 — pad to 32 so lightDir aligns to 16-byte boundary
    glm::vec3 lightDir;    // offset 32
    float _padding1;       // offset 44
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
    bool init(gpu::GPUDevice& device, gpu::PipelineRegistry& pipeline_registry,
              float world_width, float world_height);
    
    /**
     * Set heightmap from server data. Uploads to GPU texture.
     */
    void set_heightmap(const engine::Heightmap& heightmap);
    
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
    void render(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd,
                const glm::mat4& view, const glm::mat4& projection,
                const glm::vec3& camera_pos,
                const glm::vec3& light_dir);
    
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
    gpu::GPUTexture* grass_texture() { return grass_texture_.get(); }
    
    // Settings
    void set_fog_color(const glm::vec3& color) { fog_color_ = color; }
    void set_fog_range(float start, float end) { fog_start_ = start; fog_end_ = end; }
    
    /**
     * Set anisotropic filter level for terrain textures.
     * Recreates the grass sampler with the new anisotropy level.
     */
    void set_anisotropic_filter(float level);
    
private:
    void generate_terrain_mesh();
    void load_grass_texture();
    void upload_heightmap_texture();
    
    gpu::GPUDevice* device_ = nullptr;
    gpu::PipelineRegistry* pipeline_registry_ = nullptr;
    
    float world_width_ = 0.0f;
    float world_height_ = 0.0f;
    
    // Server-provided heightmap (CPU side for sampling)
    std::unique_ptr<engine::Heightmap> heightmap_;
    
    // GPU resources
    std::unique_ptr<gpu::GPUTexture> heightmap_texture_;
    std::unique_ptr<gpu::GPUTexture> grass_texture_;
    std::unique_ptr<gpu::GPUSampler> grass_sampler_;
    std::unique_ptr<gpu::GPUBuffer> vertex_buffer_;
    std::unique_ptr<gpu::GPUBuffer> index_buffer_;
    uint32_t index_count_ = 0;
    
    // Fog settings
    glm::vec3 fog_color_ = glm::vec3(0.35f, 0.45f, 0.6f);
    float fog_start_ = 800.0f;
    float fog_end_ = 4000.0f;
};

} // namespace mmo::engine::render
