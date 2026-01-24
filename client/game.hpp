#pragma once

#include "common/protocol.hpp"
#include "common/ecs/components.hpp"
#include "network_client.hpp"
#include "renderer.hpp"
#include "render/text_renderer.hpp"
#include "input_handler.hpp"
#include "systems/interpolation_system.hpp"
#include <entt/entt.hpp>
#include <string>
#include <unordered_map>
#include <vector>

namespace mmo {

enum class GameState {
    ClassSelect,
    Connecting,
    Playing
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
    
    void on_connection_accepted(const std::vector<uint8_t>& payload);
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
    
    uint32_t local_player_id_ = 0;
    PlayerClass local_player_class_ = PlayerClass::Warrior;
    int selected_class_index_ = 0;
    std::string player_name_;
    std::string host_;
    uint16_t port_ = 0;
    bool running_ = false;
    float connecting_timer_ = 0.0f;
    
    uint64_t last_frame_time_ = 0;
    float fps_ = 0.0f;
    int frame_count_ = 0;
    uint64_t fps_timer_ = 0;
};

} // namespace mmo
