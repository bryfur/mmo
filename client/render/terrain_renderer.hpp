#pragma once

#include <bgfx/bgfx.h>
#include <glm/glm.hpp>
#include <memory>

namespace mmo {

/**
 * TerrainRenderer handles procedural terrain generation and rendering using bgfx.
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
     * @param view_id The bgfx view to render into
     */
    void render(bgfx::ViewId view_id, const glm::mat4& view, const glm::mat4& projection,
                const glm::vec3& camera_pos, const glm::mat4& light_space_matrix,
                bgfx::TextureHandle shadow_map, bool shadows_enabled,
                bgfx::TextureHandle ssao_texture, bool ssao_enabled,
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
    void load_shaders();
    
    float world_width_ = 0.0f;
    float world_height_ = 0.0f;
    
    // Shader program
    bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
    
    // Terrain mesh
    bgfx::VertexBufferHandle vbh_ = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle ibh_ = BGFX_INVALID_HANDLE;
    uint32_t index_count_ = 0;
    
    // Vertex layout
    bgfx::VertexLayout layout_;
    
    // Texture
    bgfx::TextureHandle grass_texture_ = BGFX_INVALID_HANDLE;
    
    // Uniform handles
    bgfx::UniformHandle u_viewProj_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_cameraPos_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_fogParams_ = BGFX_INVALID_HANDLE;  // fogColor.rgb, fogStart in w
    bgfx::UniformHandle u_fogParams2_ = BGFX_INVALID_HANDLE; // fogEnd, padding
    bgfx::UniformHandle u_lightSpaceMatrix_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_lightDir_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_screenParams_ = BGFX_INVALID_HANDLE; // screenSize, shadowsEnabled, ssaoEnabled
    bgfx::UniformHandle s_grassTexture_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_shadowMap_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_ssaoTexture_ = BGFX_INVALID_HANDLE;
    
    // Fog settings
    glm::vec3 fog_color_ = glm::vec3(0.35f, 0.45f, 0.6f);
    float fog_start_ = 800.0f;
    float fog_end_ = 4000.0f;
};

} // namespace mmo
