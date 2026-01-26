#pragma once

#include "common/protocol.hpp"
#include "common/ecs/components.hpp"
#include "common/heightmap.hpp"
#include "model_loader.hpp"
#include "shader.hpp"
#include "render/grass_renderer.hpp"
#include "systems/camera_system.hpp"
#include "render/render_context.hpp"
#include "render/terrain_renderer.hpp"
#include "render/world_renderer.hpp"
#include "render/ui_renderer.hpp"
#include "render/effect_renderer.hpp"
#include "render/shadow_system.hpp"
#include "scene/render_scene.hpp"
#include "scene/ui_scene.hpp"
#include "gpu/pipeline_registry.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <string>
#include <vector>
#include <memory>

namespace mmo {

/**
 * Renderer is the main facade that orchestrates all rendering subsystems.
 * It maintains the public API while delegating to focused subsystems.
 */
class Renderer {
public:
    Renderer();
    ~Renderer();
    
    bool init(int width, int height, const std::string& title);
    void shutdown();
    
    bool load_models(const std::string& assets_path);
    
    void begin_frame();
    void end_frame();
    
    // ========== Scene-Based Rendering API ==========
    // This is the preferred API for rendering - populate scenes then call render()
    
    /**
     * Render a complete frame from RenderScene and UIScene.
     * This is the primary scene-based rendering method that replaces direct draw calls.
     */
    void render(const RenderScene& scene, const UIScene& ui_scene);
    
    /**
     * Render the shadow pass from RenderScene.
     */
    void render_shadow_pass(const RenderScene& scene);
    
    /**
     * Render the UI from UIScene (call between begin_ui/end_ui).
     */
    void render_ui(const UIScene& ui_scene);
    
    // ========== Legacy Direct Rendering API ==========
    // These methods are still available for compatibility but should be
    // replaced with scene-based rendering over time.
    
    // Shadow pass - call these before begin_frame() to populate shadow map
    void begin_shadow_pass();
    void end_shadow_pass();
    void draw_entity_shadow(const EntityState& entity);
    void draw_model_shadow(Model* model, const glm::vec3& position, float rotation, float scale);
    void draw_mountain_shadows();
    void draw_tree_shadows();
    
    // 3D entity rendering
    void draw_entity(const EntityState& entity, bool is_local);
    void draw_player(const PlayerState& player, bool is_local);
    void draw_model(Model* model, const glm::vec3& position, float rotation, float scale, 
                    const glm::vec4& tint, float attack_tilt = 0.0f);
    void draw_model_no_fog(Model* model, const glm::vec3& position, float rotation, float scale, 
                           const glm::vec4& tint);
    
    // World rendering - delegates to subsystems
    void draw_skybox();
    void draw_distant_mountains();
    void draw_rocks();
    void draw_trees();
    void draw_ground();
    void draw_grass();
    void draw_grid();
    
    // Attack effects - delegates to EffectRenderer
    void draw_attack_effect(const ecs::AttackEffect& effect);
    void draw_warrior_slash(float x, float y, float dir_x, float dir_y, float progress);
    void draw_mage_beam(float x, float y, float dir_x, float dir_y, float progress, float range);
    void draw_paladin_aoe(float x, float y, float dir_x, float dir_y, float progress, float range);
    void draw_archer_arrow(float x, float y, float dir_x, float dir_y, float progress, float range);
    
    // UI rendering - delegates to UIRenderer
    void begin_ui();
    void end_ui();
    void draw_button(float x, float y, float w, float h, const std::string& label, 
                     uint32_t color, bool selected);
    void draw_ui_text(const std::string& text, float x, float y, float scale, uint32_t color);
    void draw_class_preview(PlayerClass player_class, float x, float y, float size);
    void draw_filled_rect(float x, float y, float w, float h, uint32_t color);
    void draw_rect_outline(float x, float y, float w, float h, uint32_t color, float line_width = 2.0f);
    void draw_circle(float x, float y, float radius, uint32_t color, int segments = 24);
    void draw_circle_outline(float x, float y, float radius, uint32_t color, 
                             float line_width = 2.0f, int segments = 24);
    void draw_line(float x1, float y1, float x2, float y2, uint32_t color, float line_width = 2.0f);
    void draw_target_reticle();
    
    // Health bars
    void draw_player_health_ui(float health_ratio, float max_health);
    void draw_enemy_health_bar_3d(float world_x, float world_y, float world_z, 
                                   float width, float health_ratio);
    
    // Legacy compatibility
    void draw_text(const std::string& text, float x, float y, uint32_t color = 0xFFFFFFFF);
    
    // Camera control
    void set_camera(float x, float y);
    void set_camera_velocity(float vx, float vy);
    void set_camera_orbit(float yaw, float pitch);
    void adjust_camera_zoom(float delta);
    void update_camera_smooth(float dt);
    
    float get_camera_yaw() const { return camera_system_.get_yaw(); }
    float get_camera_pitch() const { return camera_system_.get_pitch(); }
    glm::vec3 get_camera_position() const { return camera_system_.get_position(); }
    
    CameraSystem& get_camera_system() { return camera_system_; }
    const CameraSystem& get_camera_system() const { return camera_system_; }
    
    // Combat feedback for camera
    void notify_player_attack();
    void notify_player_hit(float dir_x, float dir_y, float damage);
    void set_in_combat(bool in_combat);
    void set_sprinting(bool sprinting);
    
    int width() const { return context_.width(); }
    int height() const { return context_.height(); }
    
    // Animation support
    void update_animations(float dt);
    void set_entity_animation(const std::string& model_name, const std::string& anim_name);
    
    // Terrain height access (for external systems)
    float get_terrain_height(float x, float z) const { return terrain_.get_height(x, z); }
    
    // Heightmap from server
    void set_heightmap(const HeightmapChunk& heightmap);
    
    // Graphics settings toggles
    void set_shadows_enabled(bool enabled);
    void set_ssao_enabled(bool enabled);
    void set_fog_enabled(bool enabled);
    void set_grass_enabled(bool enabled);
    void set_skybox_enabled(bool enabled) { skybox_enabled_ = enabled; }
    void set_mountains_enabled(bool enabled) { mountains_enabled_ = enabled; }
    void set_trees_enabled(bool enabled) { trees_enabled_ = enabled; }
    void set_rocks_enabled(bool enabled) { rocks_enabled_ = enabled; }
    void set_anisotropic_filter(int level);  // 0=off, 1=2x, 2=4x, 3=8x, 4=16x
    void set_vsync_mode(int mode) { context_.set_vsync_mode(mode); }  // 0=off, 1=vsync, 2=triple buffer
    
    bool get_shadows_enabled() const;
    bool get_ssao_enabled() const;
    bool get_fog_enabled() const { return fog_enabled_; }
    bool get_grass_enabled() const { return grass_enabled_; }
    bool get_skybox_enabled() const { return skybox_enabled_; }
    bool get_mountains_enabled() const { return mountains_enabled_; }
    bool get_trees_enabled() const { return trees_enabled_; }
    bool get_rocks_enabled() const { return rocks_enabled_; }
    
private:
    void init_shaders();
    void init_billboard_buffers();
    void update_camera();
    Model* get_model_for_entity(const EntityState& entity);
    
    // ========== SUBSYSTEMS ==========
    RenderContext context_;
    gpu::PipelineRegistry pipeline_registry_;
    TerrainRenderer terrain_;
    WorldRenderer world_;
    UIRenderer ui_;
    EffectRenderer effects_;
    ShadowSystem shadows_;
    SSAOSystem ssao_;
    
    // ========== CAMERA ==========
    CameraSystem camera_system_;
    float camera_x_ = 0.0f;
    float camera_y_ = 0.0f;
    glm::vec3 actual_camera_pos_ = glm::vec3(0.0f);
    
    // ========== MATRICES ==========
    glm::mat4 projection_;
    glm::mat4 view_;
    
    // ========== LIGHTING ==========
    glm::vec3 sun_direction_ = glm::normalize(glm::vec3(0.5f, 0.8f, 0.3f));
    glm::vec3 light_dir_ = glm::vec3(-0.5f, -0.8f, -0.3f);
    
    // ========== SHADERS (for entity rendering) ==========
    std::unique_ptr<Shader> model_shader_;
    std::unique_ptr<Shader> skinned_model_shader_;
    std::unique_ptr<Shader> billboard_shader_;
    
    // ========== BILLBOARD VAO (for 3D health bars) ==========
    GLuint billboard_vao_ = 0;
    GLuint billboard_vbo_ = 0;
    
    // ========== GRASS ==========
    std::unique_ptr<GrassRenderer> grass_renderer_;
    float skybox_time_ = 0.0f;
    
    // World-space terrain height texture for grass placement
    // Contains Y height values indexed by world XZ coordinates
    GLuint terrain_height_texture_ = 0;
    int terrain_height_texture_size_ = 1024;  // Resolution of height texture
    void create_terrain_height_texture();
    void update_terrain_height_texture();
    void cleanup_terrain_height_texture();
    bool terrain_height_dirty_ = true;  // Flag to regenerate when heightmap changes
    
    // ========== GRAPHICS SETTINGS ==========
    bool fog_enabled_ = true;
    bool grass_enabled_ = true;
    bool skybox_enabled_ = true;
    bool mountains_enabled_ = true;
    bool trees_enabled_ = true;
    bool rocks_enabled_ = true;
    int anisotropic_level_ = 4;  // 0=off, 1=2x, 2=4x, 3=8x, 4=16x
    
    // ========== MODELS ==========
    std::unique_ptr<ModelManager> model_manager_;
    bool models_loaded_ = false;
};

} // namespace mmo
