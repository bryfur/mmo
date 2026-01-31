#pragma once

#include "common/protocol.hpp"
#include "common/ecs/components.hpp"
#include "common/heightmap.hpp"
#include "network_client.hpp"
#include "engine/application.hpp"
#include "network_smoother.hpp"
#include "engine/scene/render_scene.hpp"
#include "engine/scene/ui_scene.hpp"
#include "engine/scene/scene_renderer.hpp"
#include "engine/render/render_context.hpp"
#include "engine/systems/camera_system.hpp"
#include "game_state.hpp"
#include "graphics_settings.hpp"
#include "controls_settings.hpp"
#include "menu_types.hpp"
#include <glm/glm.hpp>
#include <entt/entt.hpp>
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>

namespace mmo {

class Game : public engine::Application {
public:
    Game();
    ~Game();

    bool init(const std::string& host, uint16_t port);
    void shutdown();

protected:
    bool on_init() override;
    void on_shutdown() override;
    void on_update(float dt) override;
    void on_render() override;

private:
    void handle_network_message(MessageType type, const std::vector<uint8_t>& payload);

    // State-specific update/render
    void update_connecting(float dt);
    void render_connecting();
    void update_class_select(float dt);
    void render_class_select();
    void update_spawning(float dt);
    void render_spawning();
    void update_playing(float dt);
    void render_playing();

    // Menu system
    void update_menu(float dt);
    void init_menu_items();
    void init_main_menu();
    void init_controls_menu();
    void init_graphics_menu();
    void apply_graphics_settings();
    void apply_controls_settings();

    void on_connection_accepted(const std::vector<uint8_t>& payload);
    void on_class_list(const std::vector<uint8_t>& payload);
    void on_heightmap_chunk(const std::vector<uint8_t>& payload);
    void on_world_state(const std::vector<uint8_t>& payload);
    void on_player_joined(const std::vector<uint8_t>& payload);
    void on_player_left(const std::vector<uint8_t>& payload);

    entt::entity find_or_create_entity(uint32_t network_id);
    void update_entity_from_state(entt::entity entity, const NetEntityState& state);
    void remove_entity(uint32_t network_id);

    // Attack effects
    void spawn_attack_effect(const NetEntityState& state, float dir_x, float dir_y);
    void update_attack_effects(float dt);

    // Scene building - populates RenderScene/UIScene for SceneRenderer
    void add_entity_to_scene(entt::entity entity, bool is_local);
    void build_class_select_ui(UIScene& ui);
    void build_connecting_ui(UIScene& ui);
    void build_playing_ui(UIScene& ui);
    void build_menu_ui(UIScene& ui);

    // Asset loading
    bool load_models(const std::string& assets_path);

    // Camera
    void update_camera_smooth(float dt);
    engine::CameraState get_camera_state() const;

    // ========== ENGINE SUBSYSTEMS ==========
    RenderContext context_;
    engine::SceneRenderer scene_renderer_;
    CameraSystem camera_system_;

    // ========== GAME STATE ==========
    NetworkClient network_;
    NetworkSmoother network_smoother_;

    RenderScene render_scene_;
    UIScene ui_scene_;

    GameState game_state_ = GameState::Connecting;
    entt::registry registry_;
    std::unordered_map<uint32_t, entt::entity> network_to_entity_;
    std::vector<ecs::AttackEffect> attack_effects_;

    std::unordered_map<uint32_t, bool> prev_attacking_;

    // Menu system
    bool menu_open_ = false;
    int menu_selected_index_ = 0;
    MenuPage current_menu_page_ = MenuPage::Main;
    std::vector<MenuItem> menu_items_;
    GraphicsSettings graphics_settings_;
    ControlsSettings controls_settings_;

    int prev_menu_selected_ = -1;
    float menu_highlight_progress_ = 1.0f;
    int prev_class_selected_ = -1;
    float class_select_highlight_progress_ = 1.0f;

    uint32_t local_player_id_ = 0;
    int selected_class_index_ = 0;
    std::vector<ClassInfo> available_classes_;
    std::string player_name_;
    std::string host_;
    uint16_t port_ = 0;
    float connecting_timer_ = 0.0f;

    NetWorldConfig world_config_;
    bool world_config_received_ = false;

    std::unique_ptr<HeightmapChunk> heightmap_;
    bool heightmap_received_ = false;

    // Camera state
    float player_x_ = 0.0f;
    float player_z_ = 0.0f;

};

} // namespace mmo
