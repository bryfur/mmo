#include "game.hpp"
#include "client/ecs/components.hpp"
#include "client/game_state.hpp"
#include "client/menu_system.hpp"
#include "client/skill_data.hpp"
#include "client/systems/animation_system.hpp"
#include "client/systems/minimap_system.hpp"
#include "client/systems/npc_interaction.hpp"
#include "client/hud/debug_hud.hpp"
#include "client/hud/floating_text.hpp"
#include "client/hud/npc_dialogue.hpp"
#include "client/hud/panels.hpp"
#include "client/hud/quest_markers.hpp"
#include "client/hud/screens.hpp"
#include "client/hud/world_projection.hpp"
#include "engine/scene/camera_state.hpp"
#include "engine/scene/ui_scene.hpp"
#include "engine/systems/camera_controller.hpp"
#include "engine/model_loader.hpp"
#include "engine/procedural/tree_generator.hpp"
#include "engine/procedural/rock_generator.hpp"
#include "engine/model_utils.hpp"
#include "engine/animation/animation_state_machine.hpp"
#include "engine/animation/ik_solver.hpp"
#include "client/ui_colors.hpp"
#include "engine/heightmap.hpp"
#include "entt/entity/fwd.hpp"
#include "glm/ext/matrix_float4x4.hpp"
#include "glm/ext/matrix_transform.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "protocol/gameplay_msgs.hpp"
#include "protocol/heightmap.hpp"
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_scancode.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <memory>
#include <string>
#include <random>
#include <cstring>
#include <cmath>
#include <vector>
#include <SDL3/SDL.h>
#include <sys/stat.h>

namespace mmo::client {

using namespace mmo::protocol;
using namespace mmo::engine;
using namespace mmo::engine::scene;
using namespace mmo::engine::systems;

// Try to locate a data directory by searching current, parent, and grandparent dirs.
// Returns the first path where the directory exists, or the original relative path as fallback.
static std::string find_data_path(const std::string& relative) {
    static const char* prefixes[] = {"", "../", "../../"};
    for (const char* prefix : prefixes) {
        std::string candidate = std::string(prefix) + relative;
        // Use SDL_GetBasePath-agnostic stat check via fopen of a sentinel
        // but since these are directories, just try them - the caller validates
        // We use a simple approach: return the first prefix where the path exists
        struct stat st;
        if (::stat(candidate.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
            return candidate;
        }
    }
    return relative;  // fallback: return as-is
}

static std::string generate_random_name() {
    static const char* adjectives[] = {"Swift", "Brave", "Clever", "Mighty", "Silent", "Bold", "Wild", "Fierce"};
    static const char* nouns[] = {"Knight", "Mage", "Rogue", "Hunter", "Warrior", "Scout", "Ranger", "Wizard"};

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> adj_dist(0, 7);
    std::uniform_int_distribution<> noun_dist(0, 7);
    std::uniform_int_distribution<> num_dist(1, 999);

    return std::string(adjectives[adj_dist(gen)]) + nouns[noun_dist(gen)] + std::to_string(num_dist(gen));
}

Game::Game() {
    player_name_ = generate_random_name();

    // Initialize camera configurations
    exploration_camera_config_ = {
        .distance = 280.0f,
        .height_offset = 90.0f,
        .shoulder_offset = 40.0f,
        .fov = 55.0f,
        .position_lag = 0.001f,
        .rotation_lag = 0.001f,
        .look_ahead_dist = 60.0f,
        .pitch_min = -70.0f,
        .pitch_max = 70.0f,
        .auto_return_speed = 1.5f,
        .auto_center_enabled = true
    };

    sprint_camera_config_ = {
        .distance = 320.0f,
        .height_offset = 70.0f,
        .shoulder_offset = 30.0f,
        .fov = 62.0f,
        .position_lag = 0.001f,
        .rotation_lag = 0.001f,
        .look_ahead_dist = 100.0f,
        .pitch_min = -70.0f,
        .pitch_max = 70.0f,
        .auto_return_speed = 3.0f,
        .auto_center_enabled = true
    };

    combat_camera_config_ = {
        .distance = 220.0f,
        .height_offset = 75.0f,
        .shoulder_offset = 50.0f,
        .fov = 52.0f,
        .position_lag = 0.001f,
        .rotation_lag = 0.001f,
        .look_ahead_dist = 40.0f,
        .pitch_min = -70.0f,
        .pitch_max = 70.0f,
        .auto_return_speed = 2.5f,
        .auto_center_enabled = false
    };
}

Game::~Game() {
    shutdown();
}

bool Game::init(const std::string& host, uint16_t port) {
    if (!init_engine()) {
        return false;
    }

    host_ = host;
    port_ = port;

    return on_init();
}

bool Game::on_init() {
    if (!init_renderer(1280, 720, "MMO Client - Select Class")) {
        return false;
    }

    input_bindings_ = std::make_unique<InputBindings>(input());
    combat_camera_ = std::make_unique<CombatCamera>(camera());

    load_models(find_data_path("assets"));

    // Load effect definitions
    effect_registry_.load_effects_directory(find_data_path("data/effects"));

    // Load animation configs
    animation_registry_.load_directory(find_data_path("data/animations"));

    // Create network message handler for gameplay messages
    msg_handler_ = std::make_unique<NetworkMessageHandler>(
        NetworkMessageHandler::Context{
            hud_state_, panel_state_, npc_interaction_,
            npcs_with_quests_, npcs_with_turnins_,
            local_player_id_, player_dead_
        });

    network_.set_message_callback(
        [this](MessageType type, const std::vector<uint8_t>& payload) {
            handle_network_message(type, payload);
        });

    game_state_ = GameState::Connecting;
    connecting_timer_ = 0.0f;

    menu_system_ = std::make_unique<MenuSystem>(input(), [this]() { quit(); }, max_vsync_mode());
    // Seed menu with persisted settings loaded by init_renderer()
    menu_system_->graphics_settings() = graphics_settings();
    {
        auto modes = available_resolutions();
        std::vector<MenuSystem::ResolutionOption> res_opts;
        for (const auto& m : modes) {
            res_opts.push_back({m.w, m.h});
        }
        menu_system_->set_available_resolutions(res_opts);
    }

    // Pre-allocate network processing buffers to avoid per-frame allocations
    to_remove_buffer_.reserve(100);

    if (!network_.connect(host_, port_, player_name_)) {
        std::cerr << "Failed to connect to server" << std::endl;
        return false;
    }

    return true;
}

void Game::shutdown() {
    on_shutdown();
    shutdown_engine();
}

void Game::on_shutdown() {
    network_.disconnect();
    shutdown_renderer();
}

void Game::on_update(float dt) {
    last_dt_ = dt;  // Store for use in render functions

    if (input_bindings_) {
        input_bindings_->update();
    }

    if (game_state_ == GameState::Playing || game_state_ == GameState::ClassSelect) {
        // If ESC is pressed while a dialog or panel is open, close it instead of opening the menu
        if (game_state_ == GameState::Playing && input().menu_toggle_pressed()) {
            bool consumed = false;
            if (npc_interaction_.showing_dialogue) {
                if (npc_interaction_.showing_quest_detail) {
                    npc_interaction_.showing_quest_detail = false;
                } else {
                    npc_interaction_.close();
                }
                consumed = true;
            } else if (hud_state_.dialogue.visible) {
                hud_state_.dialogue.visible = false;
                consumed = true;
            } else if (panel_state_.is_panel_open()) {
                panel_state_.close_all();
                consumed = true;
            }
            if (consumed) {
                input().clear_menu_inputs();
            }
        }

        menu_system_->update(dt);
        if (menu_system_->settings_dirty()) {
            apply_graphics_settings();
            apply_controls_settings();
            menu_system_->clear_settings_dirty();
        }
    }

    switch (game_state_) {
        case GameState::Connecting:
            update_connecting(dt);
            break;
        case GameState::ClassSelect:
            update_class_select(dt);
            break;
        case GameState::Spawning:
            update_spawning(dt);
            break;
        case GameState::Playing:
            update_playing(dt);
            break;
    }

    // End-of-frame: clear one-shot Enter suppression flag.
    suppress_enter_this_frame_ = false;
}

void Game::on_render() {
    bool debug_hud = menu_system_ && menu_system_->graphics_settings().show_debug_hud;
    set_collect_render_stats(debug_hud);
    network_.set_collect_stats(debug_hud);

    switch (game_state_) {
        case GameState::Connecting:
            render_connecting();
            break;
        case GameState::ClassSelect:
            render_class_select();
            break;
        case GameState::Spawning:
            render_spawning();
            break;
        case GameState::Playing:
            render_playing();
            break;
    }
}

void Game::update_class_select(float dt) {
    network_.poll_messages();

    class_select_highlight_progress_ = std::min(1.0f, class_select_highlight_progress_ + dt * 8.0f);

    if (selected_class_index_ != prev_class_selected_) {
        class_select_highlight_progress_ = 0.0f;
        prev_class_selected_ = selected_class_index_;
    }

    int num_classes = static_cast<int>(available_classes_.size());
    if (num_classes == 0) return;

    auto& input_state = input().get_input();
    bool attacking = input_bindings_ && input_bindings_->attacking();
    bool any_key = input_state.move_left || input_state.move_right || attacking;

    if (key_class_select_.just_pressed(any_key)) {
        if (input_state.move_left) {
            selected_class_index_ = (selected_class_index_ + num_classes - 1) % num_classes;
        } else if (input_state.move_right) {
            selected_class_index_ = (selected_class_index_ + 1) % num_classes;
        } else if (attacking) {
            network_.send_class_select(static_cast<uint8_t>(selected_class_index_));
            game_state_ = GameState::Spawning;
            connecting_timer_ = 0.0f;
        }
    }
}


void Game::update_connecting(float dt) {
    connecting_timer_ += dt;
    network_.poll_messages();

    if (connecting_timer_ > 10.0f) {
        std::cerr << "Connection timeout" << std::endl;
        network_.disconnect();
        quit();
    }
}

void Game::update_spawning(float dt) {
    connecting_timer_ += dt;
    network_.poll_messages();

    if (connecting_timer_ > 10.0f) {
        std::cerr << "Spawn timeout" << std::endl;
        network_.disconnect();
        quit();
    }
}



void Game::update_playing(float dt) {
    if (!network_.is_connected()) {
        std::cout << "Lost connection to server" << std::endl;
        quit();
        return;
    }

    network_.poll_messages();

    // Update gameplay timers
    update_damage_numbers(dt);
    update_notifications(dt);

    // Skill cooldowns are ticked in hud_state_.update(dt) below

    // Panel interaction (ESC close is handled in on_update before menu system)
    if (!menu_system_->is_open()) {
        update_panel_input(dt);
    }

    input().set_player_screen_pos(screen_width() / 2.0f, screen_height() / 2.0f);

    send_player_input(dt);
    input().reset_changed();

    // Chat input takes the keyboard while active; everything else gates on it.
    update_chat_input();
    if (!hud_state_.chat.input_active) {
        process_global_hotkeys();
        update_vendor_input();
        process_party_invite_input();
    }

    update_npc_interaction_frame();
    process_skill_keys();
    input_bindings_->clear_edges();

    hud_state_.update(dt);
    local_player_id_ = network_.local_player_id();
    network_smoother_.update(registry_, dt);
    tick_combat_cooldowns(dt);

    // Rotation smoothing must run before the animation pass so body-lean
    // reads a fresh turn rate.
    systems::update_rotation_smoothing(registry_, dt);
    {
        const auto cam_state = get_camera_state();
        const float anim_cull_distance = menu_system_->graphics_settings().get_draw_distance();
        systems::update_animations(registry_, dt, models(), animation_registry_,
                                   [this](float x, float z) { return get_terrain_height(x, z); },
                                   cam_state.position, anim_cull_distance);
    }

    sync_local_player_to_camera_and_hud();
    update_camera_for_player();
    update_camera_smooth(dt);

    const glm::vec3 cam_forward = camera().get_forward();
    input().set_camera_forward(cam_forward.x, cam_forward.z);

    // Populate skill slots once based on selected class (placeholder until
    // the server sends authoritative skill data).
    if (hud_state_.skill_slots[0].skill_id.empty() && selected_class_index_ >= 0) {
        populate_default_skill_bar(hud_state_, selected_class_index_);
    }

    // Refresh minimap from world state (player position, nearby icons).
    auto local_it = network_to_entity_.find(local_player_id_);
    const entt::entity local_player_entity =
        (local_it != network_to_entity_.end()) ? local_it->second : entt::null;
    systems::update_minimap(registry_, hud_state_, panel_state_,
                            local_player_entity, local_player_id_, 2000.0f);
}


// ============================================================================
// Entity → Scene Command
// ============================================================================


// ============================================================================
// Network Message Handlers
// ============================================================================

void Game::spawn_attack_effect(const NetEntityState& state, float dir_x, float dir_y) {
    const mmo::engine::EffectDefinition* effect_def = effect_registry_.get_effect(state.effect_type);
    if (effect_def) {
        // Both use x,z horizontal, y up
        glm::vec3 direction(dir_x, 0.0f, dir_y);
        // Sample client-side terrain height and offset to weapon/torso height
        float terrain_y = get_terrain_height(state.x, state.z);
        glm::vec3 position(state.x, terrain_y, state.z);

        // Use effect definition's default_range (pass -1.0f)
        render_scene_.add_particle_effect_spawn(effect_def, position, direction, -1.0f);
    }
}


// ============================================================================
// Asset Loading
// ============================================================================


// ============================================================================
// Camera
// ============================================================================

void Game::update_camera_smooth(float dt) {
    camera().set_screen_size(screen_width(), screen_height());

    // Set terrain height callback once (lazy init) instead of every frame
    if (!camera_height_func_set_) {
        camera().set_terrain_height_func([this](float x, float z) {
            return get_terrain_height(x, z);
        });
        camera_height_func_set_ = true;
    }

    float terrain_y = get_terrain_height(player_x_, player_z_);
    camera().set_target(glm::vec3(player_x_, terrain_y, player_z_));
    camera().update(dt);
}

CameraState Game::get_camera_state() const {
    return camera().get_camera_state();
}

void Game::apply_graphics_settings() {
    apply_all_graphics_settings(menu_system_->graphics_settings());
}

void Game::apply_controls_settings() {
    const auto& ctrl = menu_system_->controls_settings();
    input().set_mouse_sensitivity(ctrl.mouse_sensitivity);
    input().set_controller_sensitivity(ctrl.controller_sensitivity);
    input().set_camera_x_inverted(ctrl.invert_camera_x);
    input().set_camera_y_inverted(ctrl.invert_camera_y);
}

// ============================================================================
// Scene Building Helpers
// ============================================================================



// ============================================================================
// Delta Compression Handlers
// ============================================================================


// ============================================================================
// Panel Interaction
// ============================================================================


} // namespace mmo::client
