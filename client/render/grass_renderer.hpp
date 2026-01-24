#pragma once

#include <bgfx/bgfx.h>
#include <glm/glm.hpp>
#include <vector>

namespace mmo {

/**
 * GPU-based grass renderer using bgfx.
 * Uses instanced rendering with a pre-generated grass blade mesh.
 * Each instance is a grass blade positioned via instance buffer.
 */
class GrassRenderer {
public:
    GrassRenderer();
    ~GrassRenderer();
    
    // Initialize grass system
    void init(float world_width, float world_height);
    
    // Update time for wind animation
    void update(float delta_time, float current_time);
    
    // Render grass blades around camera position
    void render(bgfx::ViewId view_id, const glm::mat4& view, const glm::mat4& projection,
                const glm::vec3& camera_pos, const glm::mat4& light_space_matrix,
                bgfx::TextureHandle shadow_map, bool shadows_enabled,
                const glm::vec3& light_dir);
    
    // Clean up resources
    void shutdown();
    
    // Wind parameters
    float wind_magnitude = 0.8f;
    float wind_wave_length = 1.2f;
    float wind_wave_period = 1.5f;
    
    // Grass parameters
    float grass_spacing = 8.0f;       // Distance between grass blades
    float grass_view_distance = 3200.0f;  // Max render distance
    
private:
    void create_blade_mesh();
    void create_instance_buffer();
    void load_shaders();
    
    // Shader program
    bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
    
    // Grass blade mesh (single blade, instanced)
    bgfx::VertexBufferHandle blade_vbh_ = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle blade_ibh_ = BGFX_INVALID_HANDLE;
    bgfx::VertexLayout blade_layout_;
    
    // Instance data (positions/properties for all grass instances)
    bgfx::VertexBufferHandle instance_vbh_ = BGFX_INVALID_HANDLE;
    bgfx::VertexLayout instance_layout_;
    uint32_t instance_count_ = 0;
    
    // Uniform handles
    bgfx::UniformHandle u_viewProj_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_cameraPos_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_lightDir_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_grassParams_ = BGFX_INVALID_HANDLE;  // time, windMag, windWaveLen, windPeriod
    bgfx::UniformHandle u_fogParams_ = BGFX_INVALID_HANDLE;    // fogColor.rgb, fogStart
    bgfx::UniformHandle u_fogParams2_ = BGFX_INVALID_HANDLE;   // fogEnd, shadowsEnabled, 0, 0
    bgfx::UniformHandle u_worldBounds_ = BGFX_INVALID_HANDLE;  // world_width, world_height, spacing, viewDist
    bgfx::UniformHandle u_lightSpaceMatrix_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_shadowMap_ = BGFX_INVALID_HANDLE;
    
    float world_width_ = 0.0f;
    float world_height_ = 0.0f;
    float current_time_ = 0.0f;
    bool initialized_ = false;
};

} // namespace mmo
