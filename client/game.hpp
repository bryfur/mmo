#pragma once

#include "common/protocol.hpp"
#include "common/ecs/components.hpp"
#include "common/heightmap.hpp"
#include "network_client.hpp"
#include "renderer.hpp"
#include "render/text_renderer.hpp"
#include "input_handler.hpp"
#include "systems/interpolation_system.hpp"
#include <entt/entt.hpp>
#include <string>
#include <unordered_map>
#include <vector>
#include <functional>
#include <memory>

namespace mmo {

enum class GameState {
    ClassSelect,
    Connecting,
    Playing
};

// Graphics settings that can be toggled at runtime
struct GraphicsSettings {
    bool shadows_enabled = true;
    bool ssao_enabled = true;
    bool fog_enabled = true;
    bool grass_enabled = true;
    bool skybox_enabled = true;
    bool mountains_enabled = true;
    bool trees_enabled = true;
    bool rocks_enabled = true;
    bool contact_shadows_enabled = true;
    
    // Quality settings
    int shadow_quality = 2;  // 0=off, 1=low, 2=high
    int grass_density = 2;   // 0=off, 1=low, 2=high
    int anisotropic_filter = 4; // 0=off, 1=2x, 2=4x, 3=8x, 4=16x
};

// Controls settings
struct ControlsSettings {
    float mouse_sensitivity = 0.35f;
    float controller_sensitivity = 2.5f;
    bool invert_camera_x = false;
    bool invert_camera_y = false;
};

// Menu pages
enum class MenuPage {
    Main,
    Controls,
    Graphics
};

// Menu item types
enum class MenuItemType {
    Toggle,
    Slider,
    FloatSlider,
    Button,
    Submenu
};

// Menu item definition
struct MenuItem {
    std::string label;
    MenuItemType type;
    bool* toggle_value = nullptr;
    int* slider_value = nullptr;
    float* float_value = nullptr;
    float float_min = 0.0f;
    float float_max = 1.0f;
    float float_step = 0.05f;
    int slider_min = 0;
    int slider_max = 2;
    std::vector<std::string> slider_labels;  // For named options like "Off", "2x", "4x", etc.
    std::function<void()> action = nullptr;
    MenuPage target_page = MenuPage::Main;
};

class Game {
public:
    Game();
    ~Game();
    
    bool init(const std::string& host, uint16_t port);
    void run();
    void shutdown();
    
private:
    void update(float dt);
    void render();
    void handle_network_message(MessageType type, const std::vector<uint8_t>& payload);
    
    // State-specific update/render
    void update_class_select(float dt);
    void render_class_select();
    void update_connecting(float dt);
    void render_connecting();
    void update_playing(float dt);
    void render_playing();
    
    // Menu system
    void update_menu(float dt);
    void render_menu();
    void init_menu_items();
    void init_main_menu();
    void init_controls_menu();
    void init_graphics_menu();
    void apply_graphics_settings();
    void apply_controls_settings();
    
    void on_connection_accepted(const std::vector<uint8_t>& payload);
    void on_heightmap_chunk(const std::vector<uint8_t>& payload);
    void on_world_state(const std::vector<uint8_t>& payload);
    void on_player_joined(const std::vector<uint8_t>& payload);
    void on_player_left(const std::vector<uint8_t>& payload);
    
    entt::entity find_or_create_entity(uint32_t network_id);
    void update_entity_from_state(entt::entity entity, const NetEntityState& state);
    void remove_entity(uint32_t network_id);
    
    // Attack effects
    void spawn_attack_effect(uint32_t attacker_id, PlayerClass attacker_class, float x, float y, float dir_x, float dir_y);
    void update_attack_effects(float dt);
    
    Renderer renderer_;
    TextRenderer text_renderer_;
    InputHandler input_;
    NetworkClient network_;
    InterpolationSystem interpolation_system_;
    
    GameState game_state_ = GameState::ClassSelect;
    entt::registry registry_;
    std::unordered_map<uint32_t, entt::entity> network_to_entity_;
    std::vector<ecs::AttackEffect> attack_effects_;
    
    // Track previous attack state to detect attack start
    std::unordered_map<uint32_t, bool> prev_attacking_;
    
    // Menu system
    bool menu_open_ = false;
    int menu_selected_index_ = 0;
    MenuPage current_menu_page_ = MenuPage::Main;
    std::vector<MenuItem> menu_items_;
    GraphicsSettings graphics_settings_;
    ControlsSettings controls_settings_;
    
    uint32_t local_player_id_ = 0;
    PlayerClass local_player_class_ = PlayerClass::Warrior;
    int selected_class_index_ = 0;
    std::string player_name_;
    std::string host_;
    uint16_t port_ = 0;
    bool running_ = false;
    float connecting_timer_ = 0.0f;
    
    // Server-provided heightmap
    std::unique_ptr<HeightmapChunk> heightmap_;
    bool heightmap_received_ = false;
    
    uint64_t last_frame_time_ = 0;
    float fps_ = 0.0f;
    int frame_count_ = 0;
    uint64_t fps_timer_ = 0;
};

} // namespace mmo
