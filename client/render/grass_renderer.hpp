#pragma once

#include <GL/glew.h>
#include <glm/glm.hpp>
#include <memory>
#include "../shader.hpp"

namespace mmo {

// GPU-based procedural grass renderer
// Grass is generated entirely in shaders - no CPU grass data
// Uses a grid of points that follow the camera, with world-space hashing for consistent placement
class GrassRenderer {
public:
    GrassRenderer();
    ~GrassRenderer();
    
    // Initialize grass system - call after OpenGL context is ready
    void init(float world_width, float world_height);
    
    // Update time for wind animation
    void update(float delta_time, float current_time);
    
    // Render grass blades around camera position
    void render(const glm::mat4& view, const glm::mat4& projection, const glm::vec3& camera_pos,
                const glm::mat4& light_space_matrix, GLuint shadow_map, bool shadows_enabled,
                const glm::vec3& light_dir);
    
    // Clean up resources
    void shutdown();
    
    // Wind parameters
    float wind_magnitude = 0.8f;
    float wind_wave_length = 1.2f;
    float wind_wave_period = 1.5f;
    
    // Grass parameters
    float grass_spacing = 8.0f;      // Distance between grass blades
    float grass_view_distance = 3200.0f;  // Max render distance
    
private:
    void create_grid_mesh();
    void load_shaders();
    
    GLuint grass_program_ = 0;
    GLuint grass_vao_ = 0;
    GLuint grass_vbo_ = 0;
    
    int grid_size_ = 0;        // Number of points in grid (grid_size_ x grid_size_)
    int vertex_count_ = 0;
    
    float world_width_ = 0.0f;
    float world_height_ = 0.0f;
    float current_time_ = 0.0f;
    bool initialized_ = false;
};

} // namespace mmo
