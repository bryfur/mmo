#pragma once

#include "protocol/heightmap.hpp"
#include "network_client.hpp"
#include "engine/application.hpp"
#include "network_smoother.hpp"
#include "engine/scene/render_scene.hpp"
#include "engine/scene/ui_scene.hpp"
#include "engine/scene/camera_state.hpp"
#include "engine/systems/camera_controller.hpp"
#include "client/effect_loader.hpp"
#include "client/animation_loader.hpp"
#include "game_state.hpp"
#include "menu_system.hpp"
#include "gameplay_hud.hpp"
#include "gameplay_panels.hpp"
#include "network_message_handler.hpp"
#include <glm/glm.hpp>
#include <entt/entt.hpp>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <memory>

namespace mmo::client {

// Edge detector for key press events (replaces static bool prev_X patterns)
struct KeyEdge {
    bool prev = false;
    bool just_pressed(bool current) { bool r = current && !prev; prev = current; return r; }
};

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
    void handle_network_message(mmo::protocol::MessageType type, const std::vector<uint8_t>& payload);

    // State-specific update/render
    void update_connecting(float dt);
    void render_connecting();
    void update_class_select(float dt);
    void render_class_select();
    void update_spawning(float dt);
    void render_spawning();
    void update_playing(float dt);
    void render_playing();

    void apply_graphics_settings();
    void apply_controls_settings();

    void on_connection_accepted(const std::vector<uint8_t>& payload);
    void on_class_list(const std::vector<uint8_t>& payload);
    void on_heightmap_chunk(const std::vector<uint8_t>& payload);
    void on_player_joined(const std::vector<uint8_t>& payload);
    void on_player_left(const std::vector<uint8_t>& payload);

    // Delta compression handlers
    void on_entity_enter(const std::vector<uint8_t>& payload);
    void on_entity_update(const std::vector<uint8_t>& payload);
    void on_entity_exit(const std::vector<uint8_t>& payload);
    void apply_delta_to_entity(entt::entity entity, const mmo::protocol::EntityDeltaUpdate& delta);

    // Panel interaction
    void update_panel_input(float dt);
    void update_damage_numbers(float dt);
    void update_notifications(float dt);

    // Panel UI rendering
    void build_notifications_ui(engine::scene::UIScene& ui);
    void build_damage_numbers_ui(engine::scene::UIScene& ui);
    void build_dialogue_ui(engine::scene::UIScene& ui);
    void build_inventory_panel_ui(engine::scene::UIScene& ui);
    void build_talent_panel_ui(engine::scene::UIScene& ui);
    void build_quest_log_panel_ui(engine::scene::UIScene& ui);

    entt::entity find_or_create_entity(uint32_t network_id);
    void update_entity_from_state(entt::entity entity, const mmo::protocol::NetEntityState& state);
    void remove_entity(uint32_t network_id);

    // Attack effects
    void spawn_attack_effect(const mmo::protocol::NetEntityState& state, float dir_x, float dir_y);

    // Scene building - populates RenderScene/UIScene for SceneRenderer
    void add_entity_to_scene(entt::entity entity, bool is_local);
    void build_class_select_ui(engine::scene::UIScene& ui);
    void build_connecting_ui(engine::scene::UIScene& ui);
    void build_playing_ui(engine::scene::UIScene& ui);


    // Asset loading
    bool load_models(const std::string& assets_path);

    // Camera
    void update_camera_smooth(float dt);
    engine::scene::CameraState get_camera_state() const;

    // ========== GAME STATE ==========
    NetworkClient network_;
    NetworkSmoother network_smoother_;

    engine::scene::RenderScene render_scene_;
    engine::scene::UIScene ui_scene_;

    GameState game_state_ = GameState::Connecting;
    entt::registry registry_;
    std::unordered_map<uint32_t, entt::entity> network_to_entity_;

    // Effect registry (loads effect definitions from JSON)
    EffectRegistry effect_registry_;

    // Animation registry (loads state machines + tuning from JSON)
    AnimationRegistry animation_registry_;

    std::unordered_map<uint32_t, bool> prev_attacking_;

    std::unique_ptr<MenuSystem> menu_system_;
    int prev_class_selected_ = -1;
    float class_select_highlight_progress_ = 1.0f;

    uint32_t local_player_id_ = 0;
    int selected_class_index_ = 0;
    std::vector<mmo::protocol::ClassInfo> available_classes_;
    std::string player_name_;
    std::string host_;
    uint16_t port_ = 0;
    float connecting_timer_ = 0.0f;

    mmo::protocol::NetWorldConfig world_config_;
    bool world_config_received_ = false;

    std::unique_ptr<mmo::protocol::HeightmapChunk> heightmap_;
    bool heightmap_received_ = false;

    float input_send_timer_ = 0.0f;
    mmo::protocol::PlayerInput last_sent_input_;

    // Camera state
    float player_x_ = 0.0f;
    float player_z_ = 0.0f;
    bool camera_height_func_set_ = false;

    // Frame timing
    float last_dt_ = 0.016f;  // Last frame's delta time for rendering

    // Camera configurations
    engine::systems::CameraModeConfig exploration_camera_config_;
    engine::systems::CameraModeConfig sprint_camera_config_;
    engine::systems::CameraModeConfig combat_camera_config_;

    // Gameplay UI state
    HUDState hud_state_;
    PanelState panel_state_;

    // NPC Quest interaction state (defined in network_message_handler.hpp)
    NPCInteractionState npc_interaction_;

    // Network message handler (handles gameplay messages: combat, quests, inventory, etc.)
    std::unique_ptr<NetworkMessageHandler> msg_handler_;

    // NPC quest availability (updated by server)
    std::unordered_set<uint32_t> npcs_with_quests_;      // NPCs that have "!" (available quest)
    std::unordered_set<uint32_t> npcs_with_turnins_;     // NPCs that have "?" (quest ready to turn in)

    // World-to-screen projection cache (updated each frame)
    glm::mat4 cached_vp_matrix_ = glm::mat4(1.0f);
    float cached_screen_w_ = 1280.0f;
    float cached_screen_h_ = 720.0f;

    // Reusable buffers for network message processing (avoids per-frame allocations)
    std::vector<uint32_t> to_remove_buffer_;

    bool player_dead_ = false;

    // Key edge detectors (replaces static bool patterns for key-down detection)
    KeyEdge key_class_select_;          // class select screen any-key
    KeyEdge key_i_, key_l_, key_t_, key_m_;  // panel toggles in update_playing
    KeyEdge key_npc_w_, key_npc_s_;     // NPC dialogue navigation W/S
    KeyEdge key_npc_enter_;             // NPC dialogue enter
    KeyEdge key_npc_q_;                 // NPC dialogue Q (decline/back)

    // Panel input key edges (update_panel_input)
    KeyEdge panel_i_, panel_t_, panel_l_, panel_e_, panel_esc_, panel_space_;
    KeyEdge panel_dlg_up_, panel_dlg_down_;
    KeyEdge panel_inv_up_, panel_inv_down_, panel_inv_enter_;
    KeyEdge panel_inv_key1_, panel_inv_key2_, panel_inv_u_;
    KeyEdge panel_talent_up_, panel_talent_down_, panel_talent_enter_;
    KeyEdge panel_quest_up_, panel_quest_down_, panel_quest_del_;
};

} // namespace mmo::client
