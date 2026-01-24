#pragma once

#include "../model_loader.hpp"
#include "render_context.hpp"
#include <bgfx/bgfx.h>
#include <glm/glm.hpp>
#include <memory>
#include <vector>
#include <functional>

namespace mmo {

/**
 * WorldRenderer handles environmental world rendering using bgfx:
 * - Skybox
 * - Mountains
 * - Rocks  
 * - Trees
 * - Grid
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
    void render_skybox(bgfx::ViewId view_id, const glm::mat4& view, const glm::mat4& projection);
    
    /**
     * Render distant mountains.
     */
    void render_mountains(bgfx::ViewId view_id, const glm::mat4& view, const glm::mat4& projection,
                          const glm::vec3& camera_pos, const glm::vec3& light_dir);
    
    /**
     * Render scattered rocks.
     */
    void render_rocks(bgfx::ViewId view_id, const glm::mat4& view, const glm::mat4& projection,
                      const glm::vec3& camera_pos, const glm::mat4& light_space_matrix,
                      bgfx::TextureHandle shadow_map, bool shadows_enabled,
                      bgfx::TextureHandle ssao_texture, bool ssao_enabled,
                      const glm::vec3& light_dir, const glm::vec2& screen_size);
    
    /**
     * Render trees.
     */
    void render_trees(bgfx::ViewId view_id, const glm::mat4& view, const glm::mat4& projection,
                      const glm::vec3& camera_pos, const glm::mat4& light_space_matrix,
                      bgfx::TextureHandle shadow_map, bool shadows_enabled,
                      bgfx::TextureHandle ssao_texture, bool ssao_enabled,
                      const glm::vec3& light_dir, const glm::vec2& screen_size);
    
    /**
     * Render debug grid.
     */
    void render_grid(bgfx::ViewId view_id, const glm::mat4& view, const glm::mat4& projection);
    
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
    
    /**
     * Get tree positions for shadow rendering.
     */
    struct TreePositionData {
        float x, y, z;
        float rotation;
        float scale;
        int tree_type;
    };
    std::vector<TreePositionData> get_tree_positions_for_shadows() const;
    
    // Accessors
    const glm::vec3& sun_direction() const { return sun_direction_; }
    const glm::vec3& light_dir() const { return light_dir_; }
    
private:
    void generate_mountain_positions();
    void generate_rock_positions();
    void generate_tree_positions();
    void create_skybox_mesh();
    void create_grid_mesh();
    void load_shaders();
    
    float get_terrain_height(float x, float z) const;
    
    float world_width_ = 0.0f;
    float world_height_ = 0.0f;
    
    ModelManager* model_manager_ = nullptr;
    std::function<float(float, float)> terrain_height_func_;
    
    // Shaders (bgfx programs)
    bgfx::ProgramHandle skybox_program_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle grid_program_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle model_program_ = BGFX_INVALID_HANDLE;
    
    // Skybox mesh
    bgfx::VertexBufferHandle skybox_vbh_ = BGFX_INVALID_HANDLE;
    bgfx::VertexLayout skybox_layout_;
    float skybox_time_ = 0.0f;
    
    // Grid mesh
    bgfx::VertexBufferHandle grid_vbh_ = BGFX_INVALID_HANDLE;
    bgfx::VertexLayout grid_layout_;
    uint32_t grid_vertex_count_ = 0;
    
    // Uniform handles - skybox
    bgfx::UniformHandle u_skybox_viewProj_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_skybox_params_ = BGFX_INVALID_HANDLE;  // time, padding...
    bgfx::UniformHandle u_skybox_sunDir_ = BGFX_INVALID_HANDLE;
    
    // Uniform handles - grid
    bgfx::UniformHandle u_grid_viewProj_ = BGFX_INVALID_HANDLE;
    
    // Uniform handles - model rendering (mountains, rocks, trees)
    bgfx::UniformHandle u_model_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_viewProj_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_cameraPos_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_lightDir_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_lightColor_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_ambientColor_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_tintColor_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_fogParams_ = BGFX_INVALID_HANDLE;    // fogColor.rgb, fogStart
    bgfx::UniformHandle u_fogParams2_ = BGFX_INVALID_HANDLE;   // fogEnd, fogEnabled, shadowsEnabled, ssaoEnabled
    bgfx::UniformHandle u_lightSpaceMatrix_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_screenParams_ = BGFX_INVALID_HANDLE;
    
    // Texture samplers
    bgfx::UniformHandle s_baseColorTexture_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_shadowMap_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_ssaoTexture_ = BGFX_INVALID_HANDLE;
    
    // Lighting
    glm::vec3 sun_direction_ = glm::normalize(glm::vec3(0.5f, 0.8f, 0.3f));
    glm::vec3 light_dir_ = glm::vec3(-0.5f, -0.8f, -0.3f);
    
    // World object positions
    std::vector<MountainPosition> mountain_positions_;
    
    struct RockPosition {
        float x, y, z;
        float rotation;
        float scale;
        int rock_type;
    };
    std::vector<RockPosition> rock_positions_;
    
    struct TreePosition {
        float x, y, z;
        float rotation;
        float scale;
        int tree_type;
    };
    std::vector<TreePosition> tree_positions_;
    
    // Fog settings
    glm::vec3 fog_color_ = glm::vec3(0.35f, 0.45f, 0.6f);
    float fog_start_ = 800.0f;
    float fog_end_ = 4000.0f;
};

} // namespace mmo
