#pragma once

#include "../shader.hpp"
#include "../model_loader.hpp"
#include <GL/glew.h>
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
 */
class WorldRenderer {
public:
    WorldRenderer();
    ~WorldRenderer();
    
    // Non-copyable
    WorldRenderer(const WorldRenderer&) = delete;
    WorldRenderer& operator=(const WorldRenderer&) = delete;
    
    /**
     * Initialize world rendering resources.
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
     * Render skybox (should be called first with depth write disabled).
     */
    void render_skybox(const glm::mat4& view, const glm::mat4& projection);
    
    /**
     * Render distant mountains.
     */
    void render_mountains(const glm::mat4& view, const glm::mat4& projection,
                          const glm::vec3& camera_pos, const glm::vec3& light_dir);
    
    /**
     * Render debug grid.
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
    
    ModelManager* model_manager_ = nullptr;
    std::function<float(float, float)> terrain_height_func_;
    
    // Shaders
    std::unique_ptr<Shader> skybox_shader_;
    std::unique_ptr<Shader> grid_shader_;
    std::unique_ptr<Shader> model_shader_;
    
    // Skybox
    GLuint skybox_vao_ = 0;
    GLuint skybox_vbo_ = 0;
    float skybox_time_ = 0.0f;
    
    // Grid
    GLuint grid_vao_ = 0;
    GLuint grid_vbo_ = 0;
    GLuint grid_vertex_count_ = 0;
    
    // Lighting
    glm::vec3 sun_direction_ = glm::normalize(glm::vec3(0.5f, 0.8f, 0.3f));
    glm::vec3 light_dir_ = glm::vec3(-0.5f, -0.8f, -0.3f);
    
    // World object positions
    std::vector<MountainPosition> mountain_positions_;
    
    // Fog settings
    glm::vec3 fog_color_ = glm::vec3(0.35f, 0.45f, 0.6f);
    float fog_start_ = 800.0f;
    float fog_end_ = 4000.0f;
};

} // namespace mmo
