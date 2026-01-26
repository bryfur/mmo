#pragma once

#include "common/heightmap.hpp"
#include "client/gpu/gpu_device.hpp"
#include "client/gpu/gpu_buffer.hpp"
#include "client/gpu/gpu_texture.hpp"
#include "client/gpu/pipeline_registry.hpp"
#include <glm/glm.hpp>
#include <memory>

namespace mmo {

/**
 * Terrain vertex format for SDL3 GPU API
 * Matches the vertex attributes expected by terrain.vert.hlsl
 */
struct TerrainVertex {
    glm::vec3 position;  // POSITION
    glm::vec2 texCoord;  // TEXCOORD0
    glm::vec4 color;     // COLOR0
};

/**
 * Terrain transform uniforms - matches terrain.vert.hlsl cbuffer
 */
struct alignas(16) TerrainTransformUniforms {
    glm::mat4 view;
    glm::mat4 projection;
    glm::vec3 cameraPos;
    float _padding0;
    glm::mat4 lightSpaceMatrix;
};

/**
 * Terrain lighting uniforms - matches terrain.frag.hlsl cbuffer
 */
struct alignas(16) TerrainLightingUniforms {
    glm::vec3 fogColor;
    float fogStart;
    float fogEnd;
    int32_t shadowsEnabled;
    int32_t ssaoEnabled;
    float _padding0;
    glm::vec3 lightDir;
    float _padding1;
    glm::vec2 screenSize;
    glm::vec2 _padding2;
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
    void set_heightmap(const HeightmapChunk& heightmap);
    
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
     * @param light_space_matrix Light space matrix for shadows
     * @param shadow_map Shadow map texture (can be nullptr)
     * @param shadow_sampler Shadow map sampler (can be nullptr)
     * @param shadows_enabled Whether shadows are enabled
     * @param ssao_texture SSAO texture (can be nullptr)
     * @param ssao_sampler SSAO sampler (can be nullptr)
     * @param ssao_enabled Whether SSAO is enabled
     * @param light_dir Light direction
     * @param screen_size Screen dimensions
     */
    void render(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd,
                const glm::mat4& view, const glm::mat4& projection,
                const glm::vec3& camera_pos, const glm::mat4& light_space_matrix,
                SDL_GPUTexture* shadow_map, SDL_GPUSampler* shadow_sampler,
                bool shadows_enabled,
                SDL_GPUTexture* ssao_texture, SDL_GPUSampler* ssao_sampler,
                bool ssao_enabled,
                const glm::vec3& light_dir, const glm::vec2& screen_size);
    
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
    
private:
    void generate_terrain_mesh();
    void load_grass_texture();
    void upload_heightmap_texture();
    
    gpu::GPUDevice* device_ = nullptr;
    gpu::PipelineRegistry* pipeline_registry_ = nullptr;
    
    float world_width_ = 0.0f;
    float world_height_ = 0.0f;
    
    // Server-provided heightmap (CPU side for sampling)
    std::unique_ptr<HeightmapChunk> heightmap_;
    
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

} // namespace mmo
