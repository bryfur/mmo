#pragma once

#include "../shader.hpp"
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <memory>
#include <functional>

namespace mmo {

/**
 * TerrainRenderer handles procedural terrain generation and rendering.
 * Includes terrain mesh, height sampling, and grass texture rendering.
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
     * @param world_width World X dimension
     * @param world_height World Z dimension
     */
    bool init(float world_width, float world_height);
    
    /**
     * Clean up terrain resources.
     */
    void shutdown();
    
    /**
     * Render the terrain mesh.
     */
    void render(const glm::mat4& view, const glm::mat4& projection,
                const glm::vec3& camera_pos, const glm::mat4& light_space_matrix,
                GLuint shadow_map, bool shadows_enabled,
                GLuint ssao_texture, bool ssao_enabled,
                const glm::vec3& light_dir, const glm::vec2& screen_size);
    
    /**
     * Get terrain height at any world position.
     * Uses multi-octave noise for natural rolling hills.
     */
    float get_height(float x, float z) const;
    
    /**
     * Get terrain normal at any world position.
     */
    glm::vec3 get_normal(float x, float z) const;
    
    // Accessors
    float world_width() const { return world_width_; }
    float world_height() const { return world_height_; }
    
private:
    void generate_terrain_mesh();
    void load_grass_texture();
    
    float world_width_ = 0.0f;
    float world_height_ = 0.0f;
    
    // Shader
    std::unique_ptr<Shader> terrain_shader_;
    
    // Terrain mesh
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLuint ibo_ = 0;
    GLuint vertex_count_ = 0;
    GLuint grass_texture_ = 0;
    
    // Fog settings
    glm::vec3 fog_color_ = glm::vec3(0.35f, 0.45f, 0.6f);
    float fog_start_ = 800.0f;
    float fog_end_ = 4000.0f;
};

} // namespace mmo
