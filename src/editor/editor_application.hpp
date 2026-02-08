#pragma once

#include "engine/application.hpp"
#include "engine/scene/render_scene.hpp"
#include "engine/scene/ui_scene.hpp"
#include "engine/scene/camera_state.hpp"
#include "engine/heightmap.hpp"
#include "server/game_config.hpp"
#include "client/ecs/components.hpp"
#include "editor_camera.hpp"
#include "editor_raycaster.hpp"
#include "editor_tools.hpp"
#include <entt/entt.hpp>
#include <string>
#include <vector>
#include <memory>
#include <cstdint>

namespace mmo::editor {

class EditorApplication : public engine::Application {
public:
    EditorApplication();
    ~EditorApplication();

    bool init();
    void shutdown();

    // Public accessors used by tools
    entt::registry& registry() { return registry_; }
    engine::Heightmap& heightmap() { return heightmap_; }
    const EditorRaycaster& raycaster() const { return raycaster_; }
    engine::scene::CameraState get_camera_state() const;

    bool cursor_on_terrain() const { return cursor_on_terrain_; }
    glm::vec3 cursor_world_pos() const { return cursor_world_pos_; }

    void mark_heightmap_dirty() { heightmap_dirty_ = true; }

    entt::entity selected_entity() const;

    // Re-expose protected Application methods for tool access
    using Application::models;
    using Application::get_terrain_height;
    using Application::screen_width;
    using Application::screen_height;

protected:
    bool on_init() override;
    void on_shutdown() override;
    void on_update(float dt) override;
    void on_render() override;
    bool on_event(const SDL_Event& event) override;

private:
    // ImGui lifecycle
    void init_imgui();
    void shutdown_imgui();
    void imgui_new_frame();
    void imgui_render(SDL_GPUCommandBuffer* cmd, SDL_GPUTexture* swapchain);
    void build_imgui_ui();

    // Asset loading
    bool load_models(const std::string& assets_path);

    // Rendering
    void build_render_scene();
    void add_entity_to_scene(entt::entity entity);

    // Input handling
    void handle_camera_input(float dt);
    void update_cursor_raycast();

    // Save/Load
    void save_world();
    void load_world();

    // Entity helpers
    void snap_entities_to_terrain();

    // Procedural generation bake
    void generate_town_entities();
    void generate_environment_entities();
    void generate_monster_entities();
    void build_generation_ui();

    // Hot-reload
    void check_hot_reload();

    // ECS registry and config
    entt::registry registry_;
    server::GameConfig config_;

    // Heightmap (mutable for terrain editing)
    engine::Heightmap heightmap_;
    bool heightmap_dirty_ = false;
    float heightmap_update_timer_ = 0.0f;

    // Camera
    EditorCamera camera_;
    bool camera_active_ = false; // RMB held = camera mode

    // Raycaster
    EditorRaycaster raycaster_;
    bool cursor_on_terrain_ = false;
    glm::vec3 cursor_world_pos_{0.0f};
    float mouse_x_ = 0.0f;
    float mouse_y_ = 0.0f;

    // Tools
    std::unique_ptr<SelectTool> select_tool_;
    std::unique_ptr<TerrainBrushTool> terrain_tool_;
    std::unique_ptr<PlacementTool> place_tool_;
    EditorTool* active_tool_ = nullptr;
    ToolType active_tool_type_ = ToolType::Select;

    // Scene descriptions (built each frame)
    engine::scene::RenderScene render_scene_;
    engine::scene::UIScene ui_scene_;

    // ImGui state
    bool imgui_initialized_ = false;

    // Save/load
    std::string save_dir_ = "data/editor_save";

    // Generation center + visualization
    float gen_center_x_ = 0.0f;
    float gen_center_z_ = 0.0f;
    bool gen_center_set_ = false;   // has user placed a center?
    bool gen_placing_ = false;      // currently in "click to place" mode
    std::vector<entt::entity> last_generated_;  // entities from last generation pass

    // Generation parameters
    struct TownGenParams {
        bool include_buildings = true;
        bool include_walls = true;
        bool include_npcs = true;
        float wall_distance = 500.0f;
        float log_spacing = 35.0f;
        float gate_width = 120.0f;
    } town_gen_;

    struct EnvironmentGenParams {
        float radius = 3000.0f;
        float min_distance = 200.0f;   // from center (inner exclusion)
        int rock_count = 150;
        float rock_min_scale = 15.0f;
        float rock_max_scale = 60.0f;
        int tree_count = 120;
        float tree_min_scale = 240.0f;
        float tree_max_scale = 560.0f;
        float tree_min_spacing = 150.0f;
        int grove_count = 4;
        int grove_size = 12;
        int seed = 12345;
    } env_gen_;

    struct MonsterGenParams {
        int count = 50;
        float safe_zone_radius = 700.0f;
        float max_radius = 3500.0f;
        int seed = 0;   // 0 = random
    } monster_gen_;

    // Hot-reload
    int64_t last_entity_mtime_ = 0;
    float reload_check_timer_ = 0.0f;
    float toast_timer_ = 0.0f;
    std::string toast_message_;
};

} // namespace mmo::editor
