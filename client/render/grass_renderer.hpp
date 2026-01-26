#pragma once

#include "../gpu/gpu_device.hpp"
#include "../gpu/gpu_buffer.hpp"
#include "../gpu/gpu_texture.hpp"
#include "../gpu/pipeline_registry.hpp"
#include <glm/glm.hpp>
#include <memory>
#include <functional>

namespace mmo {

/**
 * Grass vertex format for SDL3 GPU API
 * Matches the vertex attributes expected by grass shaders (Vertex3D format)
 */
struct GrassVertex {
    glm::vec3 position;  // POSITION
    glm::vec3 normal;    // NORMAL
    glm::vec2 texCoord;  // TEXCOORD0
    glm::vec4 color;     // COLOR0
};

/**
 * Grass transform uniforms - matches grass.vert.hlsl cbuffer
 */
struct alignas(16) GrassTransformUniforms {
    glm::mat4 view_projection;
    glm::vec3 camera_pos;
    float time;
    float wind_strength;
    glm::vec3 wind_direction;
};

/**
 * Grass lighting uniforms - matches grass.frag.hlsl cbuffer
 */
struct alignas(16) GrassLightingUniforms {
    glm::vec3 fog_color;
    float fog_start;
    float fog_end;
    int32_t fog_enabled;
    int32_t _padding[2];
};

/**
 * GPU-based procedural grass renderer (SDL3 GPU API)
 * 
 * Grass is generated based on camera position with world-space hashing for 
 * consistent placement. The geometry is generated on CPU and rendered using
 * vertex buffers. Wind animation is applied in the vertex shader.
 */
class GrassRenderer {
public:
    GrassRenderer();
    ~GrassRenderer();
    
    // Non-copyable
    GrassRenderer(const GrassRenderer&) = delete;
    GrassRenderer& operator=(const GrassRenderer&) = delete;
    
    /**
     * Initialize grass system.
     * @param device The GPU device for resource creation
     * @param pipeline_registry The pipeline registry for getting grass pipeline
     * @param world_width World X dimension
     * @param world_height World Z dimension
     */
    bool init(gpu::GPUDevice& device, gpu::PipelineRegistry& pipeline_registry,
              float world_width, float world_height);
    
    /**
     * Update time for wind animation.
     */
    void update(float delta_time, float current_time);
    
    /**
     * Render grass blades around camera position.
     * @param pass The render pass to draw into
     * @param cmd The command buffer for push constants
     * @param view View matrix
     * @param projection Projection matrix
     * @param camera_pos Camera position
     * @param light_dir Light direction
     */
    void render(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd,
                const glm::mat4& view, const glm::mat4& projection,
                const glm::vec3& camera_pos, const glm::vec3& light_dir);
    
    /**
     * Clean up resources.
     */
    void shutdown();
    
    /**
     * Set heightmap texture for terrain height reference.
     */
    void set_heightmap_texture(gpu::GPUTexture* texture) { heightmap_texture_ = texture; }
    
    /**
     * Set terrain height callback for sampling height during grass generation.
     */
    void set_terrain_height_func(std::function<float(float, float)> func) {
        terrain_height_func_ = std::move(func);
    }
    
    // Wind parameters
    float wind_magnitude = 0.8f;
    float wind_wave_length = 1.2f;
    float wind_wave_period = 1.5f;
    
    // Grass parameters
    float grass_spacing = 8.0f;           // Distance between grass blades
    float grass_view_distance = 800.0f;   // Max render distance (reduced for performance)
    
private:
    void generate_grass_geometry(const glm::vec3& camera_pos);
    float get_terrain_height(float x, float z) const;
    
    // Hash functions for procedural generation
    static float hash(glm::vec2 p);
    static glm::vec2 hash2(glm::vec2 p);
    
    gpu::GPUDevice* device_ = nullptr;
    gpu::PipelineRegistry* pipeline_registry_ = nullptr;
    
    // Dynamic buffer for grass geometry (regenerated when camera moves)
    std::unique_ptr<gpu::GPUBuffer> vertex_buffer_;
    std::unique_ptr<gpu::GPUBuffer> index_buffer_;
    uint32_t index_count_ = 0;
    
    // Grass texture and sampler
    std::unique_ptr<gpu::GPUTexture> grass_texture_;
    std::unique_ptr<gpu::GPUSampler> grass_sampler_;
    gpu::GPUTexture* heightmap_texture_ = nullptr;
    
    // Terrain height callback
    std::function<float(float, float)> terrain_height_func_;
    
    float world_width_ = 0.0f;
    float world_height_ = 0.0f;
    float current_time_ = 0.0f;
    bool initialized_ = false;
};

} // namespace mmo
