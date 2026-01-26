#pragma once

#include "../model_loader.hpp"
#include "../gpu/gpu_device.hpp"
#include "../gpu/gpu_buffer.hpp"
#include "../gpu/gpu_pipeline.hpp"
#include "../gpu/pipeline_registry.hpp"
#include <SDL3/SDL_gpu.h>
#include <glm/glm.hpp>
#include <memory>
#include <vector>
#include <functional>

namespace mmo {

/**
 * WorldRenderer handles environmental world rendering:
 * - Skybox
 * - Mountains
 * - Grid
 * 
 * Note: Rocks and trees are now rendered as server-side entities
 * 
 * SDL3 GPU Migration: This class now uses SDL3 GPU API instead of OpenGL.
 * - VAO/VBO replaced with GPUBuffer
 * - GL shaders replaced with PipelineRegistry
 * - Draw calls use SDL_DrawGPUPrimitives
 */
class WorldRenderer {
public:
    WorldRenderer();
    ~WorldRenderer();
    
    // Non-copyable
    WorldRenderer(const WorldRenderer&) = delete;
    WorldRenderer& operator=(const WorldRenderer&) = delete;
    
    /**
     * Initialize world rendering resources (SDL3 GPU API version).
     * @param device GPU device for resource creation
     * @param pipeline_registry Pipeline registry for shader pipelines
     */
    bool init(gpu::GPUDevice& device, gpu::PipelineRegistry& pipeline_registry,
              float world_width, float world_height, ModelManager* model_manager);
    
    /**
     * Legacy initialization (deprecated - for backward compatibility during migration).
     * TODO: Remove after renderer.cpp is fully migrated to SDL3 GPU API
     */
    bool init(float world_width, float world_height, ModelManager* model_manager);
    
    /**
     * Clean up resources.
     */
    void shutdown();
    
    /**
     * Set terrain height callback for proper object placement.
     */
    void set_terrain_height_func(std::function<float(float, float)> func) {
        terrain_height_func_ = std::move(func);
    }
    
    /**
     * Update time-based effects.
     */
    void update(float dt);
    
    /**
     * Render skybox (SDL3 GPU API version).
     * @param pass Active render pass
     * @param cmd Command buffer for uniform uploads
     */
    void render_skybox(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd,
                       const glm::mat4& view, const glm::mat4& projection);
    
    /**
     * Legacy render skybox (deprecated - for backward compatibility).
     * TODO: Remove after renderer.cpp is fully migrated to SDL3 GPU API
     */
    void render_skybox(const glm::mat4& view, const glm::mat4& projection);
    
    /**
     * Render distant mountains (SDL3 GPU API version).
     * @param pass Active render pass
     * @param cmd Command buffer for uniform uploads
     */
    void render_mountains(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd,
                          const glm::mat4& view, const glm::mat4& projection,
                          const glm::vec3& camera_pos, const glm::vec3& light_dir);
    
    /**
     * Legacy render mountains (deprecated - for backward compatibility).
     * TODO: Remove after renderer.cpp is fully migrated to SDL3 GPU API
     */
    void render_mountains(const glm::mat4& view, const glm::mat4& projection,
                          const glm::vec3& camera_pos, const glm::vec3& light_dir);
    
    /**
     * Render debug grid (SDL3 GPU API version).
     * @param pass Active render pass
     * @param cmd Command buffer for uniform uploads
     */
    void render_grid(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd,
                     const glm::mat4& view, const glm::mat4& projection);
    
    /**
     * Legacy render grid (deprecated - for backward compatibility).
     * TODO: Remove after renderer.cpp is fully migrated to SDL3 GPU API
     */
    void render_grid(const glm::mat4& view, const glm::mat4& projection);
    
    /**
     * Get mountain positions for shadow rendering.
     */
    struct MountainPosition {
        float x, y, z;
        float rotation;
        float scale;
        int size_type;
    };
    const std::vector<MountainPosition>& get_mountain_positions() const { return mountain_positions_; }
    
    // Accessors
    const glm::vec3& sun_direction() const { return sun_direction_; }
    const glm::vec3& light_dir() const { return light_dir_; }
    
private:
    void generate_mountain_positions();
    void create_skybox_mesh();
    void create_grid_mesh();
    
    float get_terrain_height(float x, float z) const;
    
    float world_width_ = 0.0f;
    float world_height_ = 0.0f;
    
    gpu::GPUDevice* device_ = nullptr;
    gpu::PipelineRegistry* pipeline_registry_ = nullptr;
    ModelManager* model_manager_ = nullptr;
    std::function<float(float, float)> terrain_height_func_;
    
    // Skybox
    std::unique_ptr<gpu::GPUBuffer> skybox_vertex_buffer_;
    float skybox_time_ = 0.0f;
    
    // Grid
    std::unique_ptr<gpu::GPUBuffer> grid_vertex_buffer_;
    uint32_t grid_vertex_count_ = 0;
    
    // Lighting
    glm::vec3 sun_direction_ = glm::normalize(glm::vec3(0.5f, 0.8f, 0.3f));
    glm::vec3 light_dir_ = glm::vec3(-0.5f, -0.8f, -0.3f);
    
    // World object positions
    std::vector<MountainPosition> mountain_positions_;
    
    // Fog settings
    glm::vec3 fog_color_ = glm::vec3(0.35f, 0.45f, 0.6f);
    float fog_start_ = 800.0f;
    float fog_end_ = 4000.0f;
    
    // Sampler for textures
    SDL_GPUSampler* sampler_ = nullptr;
};

} // namespace mmo
