#include "game.hpp"
#include "client/ecs/components.hpp"
#include "client/game_state.hpp"
#include "client/menu_system.hpp"
#include "engine/scene/camera_state.hpp"
#include "engine/scene/ui_scene.hpp"
#include "engine/systems/camera_controller.hpp"
#include "engine/model_loader.hpp"
#include "engine/animation/animation_state_machine.hpp"
#include "engine/animation/ik_solver.hpp"
#include "engine/render_constants.hpp"
#include "engine/heightmap.hpp"
#include "entt/entity/fwd.hpp"
#include "glm/ext/matrix_float4x4.hpp"
#include "glm/ext/matrix_transform.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
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

namespace mmo::client {

using namespace mmo::protocol;
using namespace mmo::engine;
using namespace mmo::engine::scene;
using namespace mmo::engine::systems;

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

    std::string assets_path = "assets";
    if (!load_models(assets_path)) {
        assets_path = "../assets";
        if (!load_models(assets_path)) {
            assets_path = "../../assets";
            load_models(assets_path);
        }
    }

    // Load effect definitions
    std::string effects_path = "data/effects";
    if (!effect_registry_.load_effects_directory(effects_path)) {
        effects_path = "../data/effects";
        if (!effect_registry_.load_effects_directory(effects_path)) {
            effects_path = "../../data/effects";
            effect_registry_.load_effects_directory(effects_path);
        }
    }

    // Load animation configs
    std::string anims_path = "data/animations";
    if (!animation_registry_.load_directory(anims_path)) {
        anims_path = "../data/animations";
        if (!animation_registry_.load_directory(anims_path)) {
            anims_path = "../../data/animations";
            animation_registry_.load_directory(anims_path);
        }
    }

    network_.set_message_callback(
        [this](MessageType type, const std::vector<uint8_t>& payload) {
            handle_network_message(type, payload);
        });

    game_state_ = GameState::Connecting;
    connecting_timer_ = 0.0f;

    menu_system_ = std::make_unique<MenuSystem>(input(), [this]() { quit(); }, max_vsync_mode());
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

    if (game_state_ == GameState::Playing || game_state_ == GameState::ClassSelect) {
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
    static bool key_pressed = false;

    if (input_state.move_left && !key_pressed) {
        selected_class_index_ = (selected_class_index_ + num_classes - 1) % num_classes;
        key_pressed = true;
    } else if (input_state.move_right && !key_pressed) {
        selected_class_index_ = (selected_class_index_ + 1) % num_classes;
        key_pressed = true;
    } else if (input_state.attacking && !key_pressed) {
        network_.send_class_select(static_cast<uint8_t>(selected_class_index_));
        game_state_ = GameState::Spawning;
        connecting_timer_ = 0.0f;
        key_pressed = true;
    }

    if (!input_state.move_left && !input_state.move_right && !input_state.attacking) {
        key_pressed = false;
    }
}

void Game::render_class_select() {
    player_x_ = 4000.0f;  // Town center from editor save
    player_z_ = 4000.0f;
    camera().set_yaw(0.0f);
    camera().set_pitch(30.0f);
    update_camera_smooth(last_dt_);

    render_scene_.clear();
    render_scene_.set_draw_skybox(false);
    render_scene_.set_draw_ground(false);
    render_scene_.set_draw_grass(false);
    ui_scene_.clear();

    build_class_select_ui(ui_scene_);

    menu_system_->build_ui(ui_scene_, static_cast<float>(screen_width()), static_cast<float>(screen_height()));

    render_frame(render_scene_, ui_scene_, get_camera_state(), last_dt_);
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

void Game::render_spawning() {
    render_connecting();
}

void Game::render_connecting() {
    player_x_ = 4000.0f;  // Town center from editor save
    player_z_ = 4000.0f;
    camera().set_yaw(0.0f);
    camera().set_pitch(30.0f);
    update_camera_smooth(last_dt_);

    render_scene_.clear();
    render_scene_.set_draw_skybox(false);
    render_scene_.set_draw_ground(false);
    render_scene_.set_draw_grass(false);
    ui_scene_.clear();

    build_connecting_ui(ui_scene_);

    render_frame(render_scene_, ui_scene_, get_camera_state(), last_dt_);
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

    // Tick skill cooldowns locally
    for (auto& slot : hud_state_.skill_slots) {
        if (slot.cooldown > 0.0f) {
            slot.cooldown -= dt;
            if (slot.cooldown < 0.0f) slot.cooldown = 0.0f;
        }
    }

    // Panel interaction (before menu system eats inputs)
    if (!menu_system_->is_open()) {
        update_panel_input(dt);
    }

    input().set_player_screen_pos(screen_width() / 2.0f, screen_height() / 2.0f);

    float send_interval = world_config_.tick_rate > 0 ? (1.0f / world_config_.tick_rate) : 0.05f;
    input_send_timer_ += dt;
    if (input_send_timer_ >= send_interval) {
        input_send_timer_ -= send_interval;
        auto& eng_input = input().get_input();
        PlayerInput net_input;
        net_input.move_up = eng_input.move_up;
        net_input.move_down = eng_input.move_down;
        net_input.move_left = eng_input.move_left;
        net_input.move_right = eng_input.move_right;
        net_input.move_dir_x = eng_input.move_dir_x;
        net_input.move_dir_y = eng_input.move_dir_y;
        net_input.attacking = eng_input.attacking;
        net_input.sprinting = eng_input.sprinting;
        net_input.attack_dir_x = eng_input.attack_dir_x;
        net_input.attack_dir_y = eng_input.attack_dir_y;
        network_.send_input(net_input);
        input().consume_attack();
    }
    input().reset_changed();

    // Panel key toggles (check SDL key state directly)
    {
        static bool prev_i = false, prev_l = false, prev_t = false, prev_m = false;
        const bool* keys = SDL_GetKeyboardState(nullptr);
        bool i_down = keys[SDL_SCANCODE_I];
        bool l_down = keys[SDL_SCANCODE_L];
        bool t_down = keys[SDL_SCANCODE_T];
        bool m_down = keys[SDL_SCANCODE_M];

        if (i_down && !prev_i && !menu_system_->is_open()) panel_state_.toggle_inventory();
        if (l_down && !prev_l && !menu_system_->is_open()) panel_state_.toggle_quest_log();
        if (t_down && !prev_t && !menu_system_->is_open()) panel_state_.toggle_talent_tree();
        if (m_down && !prev_m && !menu_system_->is_open()) panel_state_.toggle_world_map();

        prev_i = i_down; prev_l = l_down; prev_t = t_down; prev_m = m_down;
    }

    // NPC interaction (E key)
    if (input().interact_pressed()) {
        if (npc_interaction_.showing_dialogue) {
            if (npc_interaction_.showing_quest_detail) {
                // Accept the quest
                if (!npc_interaction_.available_quests.empty()) {
                    auto& quest = npc_interaction_.available_quests[npc_interaction_.selected_quest];

                    // Send QuestAccept to server
                    QuestAcceptMsg accept_msg;
                    std::strncpy(accept_msg.quest_id, quest.quest_id.c_str(), 31);
                    network_.send_raw(build_packet(MessageType::QuestAccept, accept_msg));

                    // Add to quest tracker on HUD
                    QuestTrackerEntry tracker;
                    tracker.quest_name = quest.quest_name;
                    for (auto& obj : quest.objectives) {
                        tracker.objectives.push_back({obj.description, 0, obj.count, false});
                    }
                    hud_state_.tracked_quests.push_back(tracker);

                    // Add objective locations to world map and minimap
                    for (auto& obj : quest.objectives) {
                        if (obj.radius > 0.0f) {
                            MapQuestMarker marker;
                            marker.quest_name = quest.quest_name;
                            marker.world_x = obj.loc_x;
                            marker.world_z = obj.loc_z;
                            marker.radius = obj.radius;
                            marker.complete = false;
                            panel_state_.map_quest_markers.push_back(marker);

                            HUDState::MinimapState::ObjectiveArea area;
                            area.world_x = obj.loc_x;
                            area.world_z = obj.loc_z;
                            area.radius = obj.radius;
                            hud_state_.minimap.objective_areas.push_back(area);
                        }
                    }

                    // Close dialogue
                    npc_interaction_.close();
                }
            } else {
                // Enter quest detail view
                if (!npc_interaction_.available_quests.empty()) {
                    npc_interaction_.showing_quest_detail = true;
                }
            }
        } else {
            // Try to interact with nearest NPC
            auto local_it = network_to_entity_.find(local_player_id_);
            if (local_it != network_to_entity_.end() && registry_.valid(local_it->second)) {
                auto& local_transform = registry_.get<ecs::Transform>(local_it->second);

                float best_dist = 200.0f;
                uint32_t best_npc_id = 0;
                std::string best_npc_name;

                auto npc_view = registry_.view<ecs::NetworkId, ecs::Transform, ecs::EntityInfo, ecs::Name>();
                for (auto entity : npc_view) {
                    auto& npc_info = npc_view.get<ecs::EntityInfo>(entity);
                    if (npc_info.type != mmo::protocol::EntityType::TownNPC) continue;

                    auto& npc_transform = npc_view.get<ecs::Transform>(entity);
                    float dx = npc_transform.x - local_transform.x;
                    float dz = npc_transform.z - local_transform.z;
                    float dist = std::sqrt(dx * dx + dz * dz);

                    if (dist < best_dist) {
                        best_dist = dist;
                        best_npc_id = npc_view.get<ecs::NetworkId>(entity).id;
                        best_npc_name = npc_view.get<ecs::Name>(entity).value;
                    }
                }

                if (best_npc_id != 0) {
                    npc_interaction_.npc_id = best_npc_id;
                    npc_interaction_.npc_name = best_npc_name;
                    npc_interaction_.available_quests.clear();
                    npc_interaction_.selected_quest = 0;
                    npc_interaction_.showing_quest_detail = false;
                    npc_interaction_.showing_dialogue = true;

                    // Send NPCInteract to server
                    NPCInteractMsg msg;
                    msg.npc_id = best_npc_id;
                    network_.send_raw(build_packet(MessageType::NPCInteract, msg));
                }
            }
        }
    }

    // Quest dialogue navigation
    if (npc_interaction_.showing_dialogue) {
        // ESC or Q to close/go back
        if (input().menu_toggle_pressed()) {
            if (npc_interaction_.showing_quest_detail) {
                npc_interaction_.showing_quest_detail = false;
            } else {
                npc_interaction_.close();
            }
            input().clear_menu_inputs(); // prevent menu from opening
        }

        // W/S to navigate quest list (use raw SDL keys since menu_up/down only work when game input disabled)
        if (!npc_interaction_.showing_quest_detail && !npc_interaction_.available_quests.empty()) {
            static bool prev_w = false, prev_s = false;
            const bool* keys = SDL_GetKeyboardState(nullptr);
            bool w_down = keys[SDL_SCANCODE_W];
            bool s_down = keys[SDL_SCANCODE_S];

            if (w_down && !prev_w) {
                npc_interaction_.selected_quest = std::max(0, npc_interaction_.selected_quest - 1);
            }
            if (s_down && !prev_s) {
                npc_interaction_.selected_quest = std::min(static_cast<int>(npc_interaction_.available_quests.size()) - 1, npc_interaction_.selected_quest + 1);
            }

            prev_w = w_down;
            prev_s = s_down;
        }

        // Q key to decline/go back from detail view
        {
            static bool prev_q = false;
            const bool* keys = SDL_GetKeyboardState(nullptr);
            bool q_down = keys[SDL_SCANCODE_Q];
            if (q_down && !prev_q) {
                if (npc_interaction_.showing_quest_detail) {
                    npc_interaction_.showing_quest_detail = false;
                } else {
                    npc_interaction_.close();
                }
            }
            prev_q = q_down;
        }
    }

    // Skill key handling (1-5)
    {
        int skill_key = input().skill_pressed();
        if (skill_key >= 1 && skill_key <= 5 && !panel_state_.any_panel_open()) {
            int slot_idx = skill_key - 1;
            auto& slot = hud_state_.skill_slots[slot_idx];
            if (slot.available && slot.cooldown <= 0.0f) {
                // Get attack direction from input
                auto& eng_input = input().get_input();

                // Send skill use to server
                SkillUseMsg msg;
                std::strncpy(msg.skill_id, slot.skill_id.c_str(), 31);
                msg.dir_x = eng_input.attack_dir_x;
                msg.dir_z = eng_input.attack_dir_y;
                network_.send_raw(build_packet(MessageType::SkillUse, msg));

                // Set local cooldown for immediate visual feedback
                slot.cooldown = slot.max_cooldown;
            }
        }
    }

    input().clear_gameplay_inputs();

    hud_state_.update(dt);

    local_player_id_ = network_.local_player_id();

    network_smoother_.update(registry_, dt);

    // Tick down attack cooldowns locally for visual effects
    {
        auto cooldown_view = registry_.view<ecs::Combat>();
        for (auto entity : cooldown_view) {
            auto& combat = cooldown_view.get<ecs::Combat>(entity);
            if (combat.current_cooldown > 0.0f) {
                combat.current_cooldown -= dt;
                if (combat.current_cooldown < 0.0f) combat.current_cooldown = 0.0f;
            }
        }
    }

    // Update rotation smoothing (before animation so lean reads fresh turn_rate)
    {
        auto rot_view = registry_.view<ecs::EntityInfo, ecs::SmoothRotation>();
        for (auto entity : rot_view) {
            auto& info = rot_view.get<ecs::EntityInfo>(entity);
            if (info.type == EntityType::Building || info.type == EntityType::Environment) continue;

            auto& smooth = rot_view.get<ecs::SmoothRotation>(entity);
            bool has_target = false;
            float target_rotation = 0.0f;

            if (info.type == EntityType::Player) {
                if (auto* attack_dir = registry_.try_get<ecs::AttackDirection>(entity)) {
                    target_rotation = std::atan2(attack_dir->x, attack_dir->y);
                    has_target = true;
                }
            } else if (auto* vel = registry_.try_get<ecs::Velocity>(entity)) {
                if (vel->x != 0.0f || vel->z != 0.0f) {
                    target_rotation = std::atan2(vel->x, vel->z);
                    has_target = true;
                }
            }

            if (has_target) {
                smooth.smooth_toward(target_rotation, dt);
            } else {
                smooth.decay_turn_rate();
            }
        }
    }

    // Update per-entity animations via state machine
    {
        namespace anim = mmo::engine::animation;
        auto anim_view = registry_.view<ecs::Velocity, ecs::EntityInfo, ecs::Combat, ecs::AnimationInstance>();
        for (auto entity : anim_view) {
            auto&& [vel, info, combat, inst] = anim_view.get(entity);

            if (info.model_name.empty()) continue;
            Model* model = models().get_model(info.model_name);
            if (!model || !model->has_skeleton) continue;

            // Lazy-init: load state machine from entity's animation config
            if (!inst.bound) {
                if (!info.animation.empty()) {
                    const auto* config = animation_registry_.get_config(info.animation);
                    if (config) {
                        inst.state_machine = config->state_machine;
                        inst.procedural = config->procedural;
                    }
                }
                inst.state_machine.bind_clips(model->animations);
                inst.bound = true;
            }

            // Feed game parameters into the state machine
            float speed_sq = vel.x * vel.x + vel.z * vel.z;
            inst.state_machine.set_float("speed", std::sqrt(speed_sq));
            inst.state_machine.set_bool("attacking", combat.is_attacking || combat.current_cooldown > 0.0f);

            // State machine evaluates transitions, drives crossfades
            inst.state_machine.update(inst.player);

            // Tick the player: advance time, compute bone matrices
            inst.player.update(model->skeleton, model->animations, dt);

            // --- Post-animation procedural pass ---
            // Attack tilt
            inst.attack_tilt = 0.0f;
            if (combat.is_attacking && combat.current_cooldown > 0.0f) {
                float progress = std::min(combat.current_cooldown / inst.procedural.attack_tilt_cooldown, 1.0f);
                inst.attack_tilt = std::sin(progress * 3.14159f) * inst.procedural.attack_tilt_max;
            }

            // Build model matrix for IK world-space queries
            auto& transform = registry_.get<ecs::Transform>(entity);
            float target_size = info.target_size;
            float model_size = model->max_dimension();
            float scale = (target_size * 1.5f) / model_size;

            auto* smooth = registry_.try_get<ecs::SmoothRotation>(entity);
            float rotation = smooth ? smooth->current : transform.rotation;

            glm::vec3 position(transform.x, transform.y, transform.z);
            glm::mat4 model_mat = glm::translate(glm::mat4(1.0f), position);
            model_mat = glm::rotate(model_mat, rotation, glm::vec3(0.0f, 1.0f, 0.0f));
            if (inst.attack_tilt != 0.0f) {
                model_mat = glm::rotate(model_mat, inst.attack_tilt, glm::vec3(1.0f, 0.0f, 0.0f));
            }
            model_mat = glm::scale(model_mat, glm::vec3(scale));
            float cx = (model->min_x + model->max_x) / 2.0f;
            float cy = model->min_y;
            float cz = (model->min_z + model->max_z) / 2.0f;
            model_mat = model_mat * glm::translate(glm::mat4(1.0f), glm::vec3(-cx, -cy, -cz));

            // Foot IK: use terrain slope relative to entity base (not absolute)
            // so ankle height doesn't cause constant sinking
            if (model->foot_ik.valid && inst.procedural.foot_ik) {
                const auto& ik = model->foot_ik;
                auto foot_world_pos = [&](int bone_idx) -> glm::vec3 {
                    return glm::vec3(model_mat * glm::vec4(glm::vec3(inst.player.world_transforms[bone_idx][3]), 1.0f));
                };
                glm::vec3 lf = foot_world_pos(ik.left_foot);
                glm::vec3 rf = foot_world_pos(ik.right_foot);
                float base_terrain = get_terrain_height(transform.x, transform.z);
                float left_offset = get_terrain_height(lf.x, lf.z) - base_terrain;
                float right_offset = get_terrain_height(rf.x, rf.z) - base_terrain;

                anim::apply_foot_ik(inst.player.bone_matrices, inst.player.world_transforms,
                                    model->skeleton, ik, model_mat, scale, left_offset, right_offset);
            }

            // Body lean
            if (model->foot_ik.valid && model->foot_ik.spine >= 0 && inst.procedural.lean) {
                float forward_lean = 0.0f;
                float lateral_lean = 0.0f;
                float speed_sq = vel.x * vel.x + vel.z * vel.z;
                if (speed_sq > 1.0f) {
                    forward_lean = std::min(std::sqrt(speed_sq) * inst.procedural.forward_lean_factor,
                                            inst.procedural.forward_lean_max);
                }
                if (smooth) {
                    lateral_lean = glm::clamp(-smooth->turn_rate * inst.procedural.lateral_lean_factor,
                                              -inst.procedural.lateral_lean_max, inst.procedural.lateral_lean_max);
                }
                anim::apply_body_lean(inst.player.bone_matrices, inst.player.world_transforms,
                                      model->skeleton, model->foot_ik.spine, forward_lean, lateral_lean);
            }
        }
    }

    auto it = network_to_entity_.find(local_player_id_);
    if (it != network_to_entity_.end() && registry_.valid(it->second)) {
        auto& transform = registry_.get<ecs::Transform>(it->second);
        player_x_ = transform.x;
        player_z_ = transform.z;

        if (auto* vel = registry_.try_get<ecs::Velocity>(it->second)) {
            camera().set_target_velocity(glm::vec3(vel->x, 0.0f, vel->z));
        }

        if (auto* combat = registry_.try_get<ecs::Combat>(it->second)) {
            camera().set_in_combat(combat->is_attacking || combat->current_cooldown > 0);
        }

        // Sync health to HUD state
        if (auto* health = registry_.try_get<ecs::Health>(it->second)) {
            hud_state_.health = health->current;
            hud_state_.max_health = health->max;
        }
    }

    camera().set_yaw(input().get_camera_yaw());
    camera().set_pitch(input().get_camera_pitch());

    bool sprinting = input().is_sprinting() &&
        (input().move_forward() || input().move_backward() ||
         input().move_left() || input().move_right());
    if (sprinting) {
        camera().set_config(sprint_camera_config_);
    } else {
        camera().set_config(exploration_camera_config_);
    }

    float zoom_delta = input().get_camera_zoom_delta();
    if (zoom_delta != 0.0f) {
        camera().adjust_zoom(zoom_delta);
    }
    input().reset_camera_deltas();

    update_camera_smooth(dt);

    glm::vec3 cam_forward = camera().get_forward();
    input().set_camera_forward(cam_forward.x, cam_forward.z);

    // Populate skill slots based on class (hardcoded for now until server sends skill data)
    if (hud_state_.skill_slots[0].skill_id.empty() && selected_class_index_ >= 0) {
        const char* class_names[] = {"warrior", "mage", "paladin", "archer"};
        const char* class_name = class_names[std::min(selected_class_index_, 3)];
        (void)class_name; // Will be used when server sends skill data

        // Hardcoded skill names per class for HUD display
        struct SkillInfo { const char* id; const char* name; float cd; };
        SkillInfo warrior_skills[] = {{"warrior_charge","Charge",8},{"warrior_ground_slam","Ground Slam",12},{"warrior_battle_cry","Battle Cry",25},{"warrior_whirlwind","Whirlwind",15},{"warrior_execute","Execute",20}};
        SkillInfo mage_skills[] = {{"mage_fireball","Fireball",6},{"mage_frost_nova","Frost Nova",10},{"mage_blink","Blink",12},{"mage_arcane_rain","Arcane Rain",18},{"mage_meteor","Meteor",25}};
        SkillInfo paladin_skills[] = {{"paladin_holy_strike","Holy Strike",5},{"paladin_consecrate","Consecrate",10},{"paladin_divine_shield","Divine Shield",30},{"paladin_healing_aura","Healing Aura",20},{"paladin_judgment","Judgment",8}};
        SkillInfo archer_skills[] = {{"archer_evasive_roll","Evasive Roll",6},{"archer_multi_shot","Multi-Shot",8},{"archer_snare_trap","Snare Trap",15},{"archer_piercing_shot","Piercing Shot",10},{"archer_rain_of_arrows","Rain of Arrows",20}};

        SkillInfo* skills = warrior_skills;
        if (selected_class_index_ == 1) skills = mage_skills;
        else if (selected_class_index_ == 2) skills = paladin_skills;
        else if (selected_class_index_ == 3) skills = archer_skills;

        for (int i = 0; i < 5; ++i) {
            hud_state_.skill_slots[i].skill_id = skills[i].id;
            hud_state_.skill_slots[i].name = skills[i].name;
            hud_state_.skill_slots[i].max_cooldown = skills[i].cd;
            hud_state_.skill_slots[i].key_number = i + 1;
            hud_state_.skill_slots[i].available = true; // All available for now
        }
    }

    // Sync HUD state from local player entity
    {
        auto pit = network_to_entity_.find(local_player_id_);
        if (pit != network_to_entity_.end() && registry_.valid(pit->second)) {
            auto entity = pit->second;
            // Note: client doesn't have PlayerLevel/Inventory components yet
            // HUD state will be populated when server sends progression messages
            // For now, just keep the HUD state as-is (it will show defaults)
            (void)entity;
        }
    }

    // Update minimap
    {
        auto local_it = network_to_entity_.find(local_player_id_);
        if (local_it != network_to_entity_.end() && registry_.valid(local_it->second)) {
            auto& lt = registry_.get<ecs::Transform>(local_it->second);
            hud_state_.minimap.player_x = lt.x;
            hud_state_.minimap.player_z = lt.z;
            panel_state_.player_x = lt.x;
            panel_state_.player_z = lt.z;
        }

        hud_state_.minimap.icons.clear();
        hud_state_.minimap.objective_areas.clear();

        // Add nearby entities to minimap
        auto entity_view = registry_.view<ecs::Transform, ecs::EntityInfo, ecs::NetworkId>();
        for (auto entity : entity_view) {
            auto& t = entity_view.get<ecs::Transform>(entity);
            auto& info = entity_view.get<ecs::EntityInfo>(entity);
            auto& nid = entity_view.get<ecs::NetworkId>(entity);

            if (nid.id == local_player_id_) continue;

            float dx = t.x - hud_state_.minimap.player_x;
            float dz = t.z - hud_state_.minimap.player_z;
            if (dx * dx + dz * dz > 2000.0f * 2000.0f) continue;

            uint32_t color = 0;
            switch (info.type) {
                case EntityType::TownNPC:     color = 0xFF00CC00; break;  // green
                case EntityType::NPC:         color = 0xFF0000FF; break;  // red
                case EntityType::Player:      color = 0xFFFFFF00; break;  // cyan
                case EntityType::Building:    color = 0xFF888888; break;  // gray
                default: continue;
            }

            HUDState::MinimapState::MapIcon icon;
            icon.world_x = t.x;
            icon.world_z = t.z;
            icon.color = color;
            icon.is_objective = false;
            hud_state_.minimap.icons.push_back(icon);
        }
    }
}

void Game::render_playing() {
    render_scene_.clear();
    ui_scene_.clear();

    const auto& gfx = menu_system_->graphics_settings();
    render_scene_.set_draw_skybox(gfx.skybox_enabled);
    render_scene_.set_draw_rocks(gfx.rocks_enabled);
    render_scene_.set_draw_trees(gfx.trees_enabled);
    render_scene_.set_draw_ground(true);
    render_scene_.set_draw_grass(gfx.grass_enabled);

    // Cache VP matrix for world-to-screen projection in UI
    auto cam_state = get_camera_state();
    cached_vp_matrix_ = cam_state.view_projection;
    cached_screen_w_ = static_cast<float>(screen_width());
    cached_screen_h_ = static_cast<float>(screen_height());

    // Add entities to scene as model commands
    // Distance cull from player position (camera is 200-350 units behind player)
    float draw_dist = gfx.get_draw_distance();
    float ENTITY_DRAW_DISTANCE_SQ = draw_dist * draw_dist;

    auto entity_view = registry_.view<ecs::NetworkId, ecs::Transform, ecs::Health, ecs::EntityInfo, ecs::Name>();
    for (auto entity : entity_view) {
        auto&& [net_id, transform, health, info, name] = entity_view.get(entity);
        bool is_local = (net_id.id == local_player_id_);

        // Filter trees/rocks by scene flags
        if (info.type == EntityType::Environment) {
            bool is_tree = (info.model_name.compare(0, 5, "tree_") == 0);
            if (is_tree && !render_scene_.should_draw_trees()) continue;
            if (!is_tree && !render_scene_.should_draw_rocks()) continue;
        }

        // Distance culling from player position (always render local player)
        if (!is_local) {
            float dx = transform.x - player_x_;
            float dz = transform.z - player_z_;
            if (dx * dx + dz * dz > ENTITY_DRAW_DISTANCE_SQ) continue;
        }

        add_entity_to_scene(entity, is_local);
    }

    build_playing_ui(ui_scene_);

    menu_system_->build_ui(ui_scene_, static_cast<float>(screen_width()), static_cast<float>(screen_height()));

    render_frame(render_scene_, ui_scene_, get_camera_state(), last_dt_);
}

// ============================================================================
// Entity → Scene Command
// ============================================================================

void Game::add_entity_to_scene(entt::entity entity, bool is_local) {
    auto& transform = registry_.get<ecs::Transform>(entity);
    auto& health = registry_.get<ecs::Health>(entity);
    auto& info = registry_.get<ecs::EntityInfo>(entity);

    if (info.model_name.empty()) return;

    Model* model = models().get_model(info.model_name);
    if (!model) return;

    // Read rotation (already smoothed in update_playing)
    float rotation = transform.rotation;
    if (info.type != EntityType::Building && info.type != EntityType::Environment) {
        if (auto* smooth = registry_.try_get<ecs::SmoothRotation>(entity)) {
            rotation = smooth->current;
        }
    }

    // Read attack tilt (already computed in update_playing)
    float attack_tilt = 0.0f;
    auto* anim_inst = registry_.try_get<ecs::AnimationInstance>(entity);
    if (anim_inst) attack_tilt = anim_inst->attack_tilt;

    // Build transform matrix
    float target_size = info.target_size;
    float model_size = model->max_dimension();
    float scale = (target_size * 1.5f) / model_size;

    glm::vec3 position(transform.x, transform.y, transform.z);
    glm::mat4 model_mat = glm::translate(glm::mat4(1.0f), position);
    model_mat = glm::rotate(model_mat, rotation, glm::vec3(0.0f, 1.0f, 0.0f));
    if (attack_tilt != 0.0f) {
        model_mat = glm::rotate(model_mat, attack_tilt, glm::vec3(1.0f, 0.0f, 0.0f));
    }
    model_mat = glm::scale(model_mat, glm::vec3(scale));

    float cx = (model->min_x + model->max_x) / 2.0f;
    float cy = model->min_y;
    float cz = (model->min_z + model->max_z) / 2.0f;
    model_mat = model_mat * glm::translate(glm::mat4(1.0f), glm::vec3(-cx, -cy, -cz));

    glm::vec4 tint(1.0f);

    // Submit draw command (bone matrices already have IK/lean applied from update_playing)
    if (model->has_skeleton && anim_inst && anim_inst->bound) {
        render_scene_.add_skinned_model(info.model_name, model_mat, anim_inst->player.bone_matrices, tint);
    } else if (model->has_skeleton) {
        static const auto identity_bones = []() {
            std::array<glm::mat4, 64> arr;
            arr.fill(glm::mat4(1.0f));
            return arr;
        }();
        render_scene_.add_skinned_model(info.model_name, model_mat, identity_bones, tint);
    } else {
        render_scene_.add_model(info.model_name, model_mat, tint, attack_tilt);
    }

    // Health bar
    bool show_health_bar = (info.type != EntityType::Building &&
                            info.type != EntityType::Environment &&
                            info.type != EntityType::TownNPC);
    if (show_health_bar && !is_local) {
        float health_ratio = health.current / health.max;
        float bar_height_offset = transform.y + target_size * 1.3f;
        render_scene_.add_billboard_3d(transform.x, bar_height_offset, transform.z,
                                       target_size * 0.8f, health_ratio,
                                       ui_colors::HEALTH_HIGH, ui_colors::HEALTH_BAR_BG, ui_colors::HEALTH_3D_BG);
    }

}

// ============================================================================
// Network Message Handlers
// ============================================================================

void Game::handle_network_message(MessageType type, const std::vector<uint8_t>& payload) {
    switch (type) {
        case MessageType::ConnectionAccepted:
            on_connection_accepted(payload);
            break;
        case MessageType::WorldConfig:
            if (payload.size() >= NetWorldConfig::serialized_size()) {
                world_config_.deserialize(payload);
                world_config_received_ = true;
                network_smoother_.set_interpolation_time(1.0f / world_config_.tick_rate);
                std::cout << "Received world config: " << world_config_.world_width << "x" << world_config_.world_height
                          << " tick_rate=" << world_config_.tick_rate << std::endl;
            }
            break;
        case MessageType::ClassList:
            on_class_list(payload);
            break;
        case MessageType::HeightmapChunk:
            on_heightmap_chunk(payload);
            break;
        case MessageType::PlayerJoined:
            on_player_joined(payload);
            break;
        case MessageType::PlayerLeft:
            on_player_left(payload);
            break;
        case MessageType::EntityEnter:
            on_entity_enter(payload);
            break;
        case MessageType::EntityUpdate:
            on_entity_update(payload);
            break;
        case MessageType::EntityExit:
            on_entity_exit(payload);
            break;
        case MessageType::XPGain: {
            if (payload.size() >= 4 * sizeof(int32_t)) {
                BufferReader r(payload);
                int32_t xp_gained = r.read<int32_t>();
                int32_t total_xp = r.read<int32_t>();
                int32_t xp_to_next = r.read<int32_t>();
                int32_t level = r.read<int32_t>();
                (void)xp_gained;

                int old_level = hud_state_.level;
                hud_state_.xp = total_xp;
                hud_state_.xp_to_next_level = xp_to_next;
                hud_state_.level = level;

                if (level > old_level) {
                    hud_state_.show_level_up(level);
                }
            }
            break;
        }
        case MessageType::LevelUp: {
            if (payload.size() >= sizeof(int32_t)) {
                BufferReader r(payload);
                int32_t new_level = r.read<int32_t>();
                hud_state_.show_level_up(new_level);
                hud_state_.level = new_level;
            }
            break;
        }
        case MessageType::GoldChange: {
            if (payload.size() >= 2 * sizeof(int32_t)) {
                BufferReader r(payload);
                int32_t gold_change = r.read<int32_t>();
                int32_t total_gold = r.read<int32_t>();
                hud_state_.gold = total_gold;
                if (gold_change > 0) {
                    char buf[64];
                    snprintf(buf, sizeof(buf), "+%d Gold", gold_change);
                    hud_state_.add_loot(buf, 0xFF00DDFF);
                }
            }
            break;
        }
        case MessageType::LootDrop: {
            if (payload.size() >= sizeof(int32_t) + 1) {
                BufferReader r(payload);
                int32_t gold = r.read<int32_t>();
                uint8_t item_count = r.read<uint8_t>();

                if (gold > 0) {
                    hud_state_.gold += gold;
                    char buf[64];
                    snprintf(buf, sizeof(buf), "+%d Gold", gold);
                    hud_state_.add_loot(buf, 0xFF00DDFF);
                }

                for (uint8_t i = 0; i < item_count && r.remaining_size() >= 49; ++i) {
                    std::string name = r.read_fixed_string(32);
                    std::string rarity = r.read_fixed_string(16);
                    uint8_t count = r.read<uint8_t>();

                    // Determine color from rarity
                    uint32_t color = 0xFFAAAAAA; // common
                    if (rarity == "uncommon") color = 0xFF00CC00;
                    else if (rarity == "rare") color = 0xFF0088FF;
                    else if (rarity == "epic") color = 0xFFCC00CC;
                    else if (rarity == "legendary") color = 0xFF00AAFF;

                    char buf[64];
                    if (count > 1)
                        snprintf(buf, sizeof(buf), "Received: %s x%d", name.c_str(), count);
                    else
                        snprintf(buf, sizeof(buf), "Received: %s", name.c_str());
                    hud_state_.add_loot(buf, color);
                }
            }
            break;
        }
        case MessageType::QuestOffer: {
            if (payload.size() >= QuestOfferMsg::serialized_size()) {
                QuestOfferMsg msg;
                msg.deserialize(payload);

                QuestOfferData offer;
                offer.quest_id = std::string(msg.quest_id, strnlen(msg.quest_id, 32));
                offer.quest_name = std::string(msg.quest_name, strnlen(msg.quest_name, 64));
                offer.description = std::string(msg.description, strnlen(msg.description, 256));
                offer.dialogue = std::string(msg.dialogue, strnlen(msg.dialogue, 256));
                offer.xp_reward = msg.xp_reward;
                offer.gold_reward = msg.gold_reward;

                for (uint8_t i = 0; i < msg.objective_count && i < QuestOfferMsg::MAX_OBJECTIVES; ++i) {
                    offer.objectives.push_back({
                        std::string(msg.objectives[i].description, strnlen(msg.objectives[i].description, 64)),
                        msg.objectives[i].count,
                        msg.objectives[i].location_x,
                        msg.objectives[i].location_z,
                        msg.objectives[i].radius
                    });
                }

                npc_interaction_.available_quests.push_back(std::move(offer));
            }
            break;
        }
        case MessageType::ZoneChange: {
            if (payload.size() >= 64) {
                char zone_buf[64] = {};
                std::memcpy(zone_buf, payload.data(), 64);
                hud_state_.set_zone(std::string(zone_buf, strnlen(zone_buf, 64)));
            }
            break;
        }
        case MessageType::QuestList: {
            if (payload.size() >= sizeof(uint16_t)) {
                npcs_with_quests_.clear();
                npcs_with_turnins_.clear();
                size_t offset = 0;
                uint16_t count;
                std::memcpy(&count, payload.data(), sizeof(uint16_t));
                offset += sizeof(uint16_t);
                for (uint16_t i = 0; i < count && offset + sizeof(uint32_t) <= payload.size(); ++i) {
                    uint32_t encoded;
                    std::memcpy(&encoded, payload.data() + offset, sizeof(uint32_t));
                    offset += sizeof(uint32_t);
                    uint32_t npc_id = encoded & 0x7FFFFFFF;
                    bool has_turnin = (encoded & 0x80000000) != 0;
                    if (has_turnin) {
                        npcs_with_turnins_.insert(npc_id);
                    } else {
                        npcs_with_quests_.insert(npc_id);
                    }
                }
            }
            break;
        }
        case MessageType::CombatEvent:
            on_combat_event(payload);
            break;
        case MessageType::EntityDeath:
            on_entity_death(payload);
            break;
        case MessageType::QuestProgress:
            on_quest_progress(payload);
            break;
        case MessageType::QuestComplete:
            on_quest_complete(payload);
            break;
        case MessageType::InventoryUpdate:
            on_inventory_update(payload);
            break;
        case MessageType::ItemEquip:
        case MessageType::ItemUnequip:
            // Server sends back InventoryUpdate after equip/unequip
            on_inventory_update(payload);
            break;
        case MessageType::SkillCooldown:
            on_skill_cooldown(payload);
            break;
        case MessageType::SkillUnlock:
            on_skill_unlock(payload);
            break;
        case MessageType::TalentSync:
            on_talent_sync(payload);
            break;
        case MessageType::NPCDialogue:
            on_npc_dialogue(payload);
            break;
        default:
            break;
    }
}

void Game::on_connection_accepted(const std::vector<uint8_t>& payload) {
    if (payload.size() >= ConnectionAcceptedMsg::serialized_size()) {
        ConnectionAcceptedMsg msg;
        msg.deserialize(payload);
        if (msg.player_id == 0) {
            std::cout << "Connection accepted, waiting for class list..." << std::endl;
        } else {
            local_player_id_ = msg.player_id;
            std::cout << "Spawned with player ID: " << local_player_id_ << std::endl;
            if (game_state_ == GameState::Spawning) {
                game_state_ = GameState::Playing;
            }
        }
    }
}

void Game::on_class_list(const std::vector<uint8_t>& payload) {
    if (payload.empty()) return;

    BufferReader r(payload);
    uint16_t count = r.get_array_size();

    // Ensure buffer has enough capacity (only reallocates if needed)
    if (available_classes_.capacity() < count) {
        available_classes_.reserve(count);
    }
    available_classes_.resize(count);

    r.read_array_into(std::span(available_classes_), count);

    std::cout << "Received " << available_classes_.size() << " classes from server" << std::endl;

    if (game_state_ == GameState::Connecting) {
        game_state_ = GameState::ClassSelect;
        selected_class_index_ = 0;
    }
}

void Game::on_heightmap_chunk(const std::vector<uint8_t>& payload) {
    heightmap_ = std::make_unique<HeightmapChunk>();
    if (heightmap_->deserialize(payload)) {
        heightmap_received_ = true;
        std::cout << "Received heightmap: " << heightmap_->resolution << "x" << heightmap_->resolution
                  << " covering " << heightmap_->world_size << "x" << heightmap_->world_size << " world units" << std::endl;

        engine::Heightmap engine_hm;
        engine_hm.resolution = heightmap_->resolution;
        engine_hm.world_origin_x = heightmap_->world_origin_x;
        engine_hm.world_origin_z = heightmap_->world_origin_z;
        engine_hm.world_size = heightmap_->world_size;
        engine_hm.min_height = heightmap_config::MIN_HEIGHT;
        engine_hm.max_height = heightmap_config::MAX_HEIGHT;
        engine_hm.height_data = heightmap_->height_data;

        set_heightmap(engine_hm);
    } else {
        std::cerr << "Failed to deserialize heightmap!" << std::endl;
        heightmap_.reset();
    }
}

void Game::on_player_joined(const std::vector<uint8_t>& payload) {
    if (payload.size() >= EntityState::serialized_size()) {
        NetEntityState state;
        state.deserialize(payload);

        entt::entity entity = find_or_create_entity(state.id);
        update_entity_from_state(entity, state);

        std::cout << "Player joined: " << state.name << " (ID: " << state.id << ")" << std::endl;
    }
}

void Game::on_player_left(const std::vector<uint8_t>& payload) {
    if (payload.size() >= PlayerLeftMsg::serialized_size()) {
        PlayerLeftMsg msg;
        msg.deserialize(payload);

        auto it = network_to_entity_.find(msg.player_id);
        if (it != network_to_entity_.end()) {
            if (registry_.valid(it->second)) {
                auto* name = registry_.try_get<ecs::Name>(it->second);
                if (name) {
                    std::cout << "Player left: " << name->value << " (ID: " << msg.player_id << ")" << std::endl;
                }
            }
            remove_entity(msg.player_id);
            prev_attacking_.erase(msg.player_id);
        }
    }
}

// ============================================================================
// ECS Entity Management
// ============================================================================

entt::entity Game::find_or_create_entity(uint32_t network_id) {
    auto it = network_to_entity_.find(network_id);
    if (it != network_to_entity_.end() && registry_.valid(it->second)) {
        return it->second;
    }

    entt::entity entity = registry_.create();
    registry_.emplace<ecs::NetworkId>(entity, network_id);
    registry_.emplace<ecs::Transform>(entity);
    registry_.emplace<ecs::Velocity>(entity);
    registry_.emplace<ecs::Health>(entity);
    registry_.emplace<ecs::EntityInfo>(entity);
    registry_.emplace<ecs::Name>(entity);
    registry_.emplace<ecs::Combat>(entity);
    registry_.emplace<ecs::Interpolation>(entity);
    registry_.emplace<ecs::SmoothRotation>(entity);
    registry_.emplace<ecs::AnimationInstance>(entity);

    network_to_entity_[network_id] = entity;
    return entity;
}

void Game::update_entity_from_state(entt::entity entity, const NetEntityState& state) {
    auto& transform = registry_.get<ecs::Transform>(entity);
    auto& velocity = registry_.get<ecs::Velocity>(entity);
    auto& health = registry_.get<ecs::Health>(entity);
    auto& info = registry_.get<ecs::EntityInfo>(entity);
    auto& name = registry_.get<ecs::Name>(entity);
    auto& combat = registry_.get<ecs::Combat>(entity);
    auto& interp = registry_.get<ecs::Interpolation>(entity);

    interp.prev_x = transform.x;
    interp.prev_y = transform.y;
    interp.prev_z = transform.z;
    interp.target_x = state.x;
    interp.target_y = state.y;
    interp.target_z = state.z;
    interp.alpha = 0.0f;

    transform.rotation = state.rotation;

    velocity.x = state.vx;
    velocity.z = state.vy;

    health.current = state.health;
    health.max = state.max_health;

    info.type = state.type;
    info.player_class = state.player_class;
    info.npc_type = state.npc_type;
    info.building_type = state.building_type;
    info.environment_type = state.environment_type;
    info.color = state.color;
    info.model_name = state.model_name;
    info.target_size = state.target_size;
    info.effect_type = state.effect_type;
    info.animation = state.animation;
    info.cone_angle = state.cone_angle;
    info.shows_reticle = state.shows_reticle;

    name.value = state.name;

    combat.is_attacking = state.is_attacking;
    combat.current_cooldown = state.attack_cooldown;

    if (!registry_.all_of<ecs::AttackDirection>(entity)) {
        registry_.emplace<ecs::AttackDirection>(entity);
    }
    auto& attack_dir = registry_.get<ecs::AttackDirection>(entity);
    attack_dir.x = state.attack_dir_x;
    attack_dir.y = state.attack_dir_y;

    if (!registry_.all_of<ecs::Scale>(entity)) {
        registry_.emplace<ecs::Scale>(entity);
    }
    registry_.get<ecs::Scale>(entity).value = state.scale;
}

void Game::remove_entity(uint32_t network_id) {
    auto it = network_to_entity_.find(network_id);
    if (it != network_to_entity_.end()) {
        if (registry_.valid(it->second)) {
            registry_.destroy(it->second);
        }
        network_to_entity_.erase(it);
    }
}

void Game::spawn_attack_effect(const NetEntityState& state, float dir_x, float dir_y) {
    const ::engine::EffectDefinition* effect_def = effect_registry_.get_effect(state.effect_type);
    if (effect_def) {
        // Both use x,z horizontal, y up
        glm::vec3 direction(dir_x, 0.0f, dir_y);
        // Sample client-side terrain height and offset to weapon/torso height
        float terrain_y = get_terrain_height(state.x, state.z);
        std::cout << "Attack effect at: x=" << state.x << " y=" << terrain_y << " state.y=" << state.y << " z=" << state.z << std::endl;
        glm::vec3 position(state.x, terrain_y, state.z);

        // Use effect definition's default_range (pass -1.0f)
        render_scene_.add_particle_effect_spawn(effect_def, position, direction, -1.0f);
    }
}


// ============================================================================
// Asset Loading
// ============================================================================

bool Game::load_models(const std::string& assets_path) {
    auto& mdl = models();
    std::string models_path = assets_path + "/models/";

    bool success = true;

    if (!mdl.load_model("warrior", models_path + "warrior_rigged.glb")) {
        success &= mdl.load_model("warrior", models_path + "warrior.glb");
    }
    if (!mdl.load_model("mage", models_path + "mage_rigged.glb")) {
        success &= mdl.load_model("mage", models_path + "mage.glb");
    }
    if (!mdl.load_model("paladin", models_path + "paladin_rigged.glb")) {
        success &= mdl.load_model("paladin", models_path + "paladin.glb");
    }
    if (!mdl.load_model("archer", models_path + "archer_rigged.glb")) {
        success &= mdl.load_model("archer", models_path + "archer.glb");
    }
    success &= mdl.load_model("npc_enemy", models_path + "npc_enemy.glb");

    mdl.load_model("ground_grass", models_path + "ground_grass.glb");
    mdl.load_model("ground_stone", models_path + "ground_stone.glb");

    mdl.load_model("mountain_small", models_path + "mountain_small.glb");
    mdl.load_model("mountain_medium", models_path + "mountain_medium.glb");
    mdl.load_model("mountain_large", models_path + "mountain_large.glb");

    mdl.load_model("building_tavern", models_path + "building_tavern.glb");
    mdl.load_model("building_blacksmith", models_path + "building_blacksmith.glb");
    mdl.load_model("building_tower", models_path + "building_tower.glb");
    mdl.load_model("building_shop", models_path + "building_shop.glb");
    mdl.load_model("building_well", models_path + "building_well.glb");
    mdl.load_model("building_house", models_path + "building_house.glb");
    mdl.load_model("building_inn", models_path + "inn.glb");
    mdl.load_model("wooden_log", models_path + "wooden_log.glb");
    mdl.load_model("log_tower", models_path + "log_tower.glb");

    mdl.load_model("npc_merchant", models_path + "npc_merchant.glb");
    mdl.load_model("npc_guard", models_path + "npc_guard.glb");
    mdl.load_model("npc_blacksmith", models_path + "npc_blacksmith.glb");
    mdl.load_model("npc_innkeeper", models_path + "npc_innkeeper.glb");
    mdl.load_model("npc_villager", models_path + "npc_villager.glb");

    mdl.load_model("weapon_sword", models_path + "weapon_sword.glb");
    mdl.load_model("spell_fireball", models_path + "spell_fireball.glb");
    mdl.load_model("spell_bible", models_path + "spell_bible.glb");

    mdl.load_model("rock_boulder", models_path + "rock_boulder.glb");
    mdl.load_model("rock_slate", models_path + "rock_slate.glb");
    mdl.load_model("rock_spire", models_path + "rock_spire.glb");
    mdl.load_model("rock_cluster", models_path + "rock_cluster.glb");
    mdl.load_model("rock_mossy", models_path + "rock_mossy.glb");

    mdl.load_model("tree_oak", models_path + "tree_oak.glb");
    mdl.load_model("tree_pine", models_path + "tree_pine.glb");
    mdl.load_model("tree_dead", models_path + "tree_dead.glb");

    if (success) {
        std::cout << "All 3D models loaded successfully" << std::endl;
    } else {
        std::cerr << "Warning: Some models failed to load" << std::endl;
    }

    return success;
}

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
    CameraState state;
    state.view = camera().get_view_matrix();
    state.projection = camera().get_projection_matrix();
    state.view_projection = state.projection * state.view;
    state.position = camera().get_position();
    return state;
}

void Game::apply_graphics_settings() {
    const auto& gfx = menu_system_->graphics_settings();
    set_graphics_settings(gfx);
    set_anisotropic_filter(gfx.anisotropic_filter);
    set_vsync_mode(gfx.vsync_mode);
    set_window_mode(gfx.window_mode, gfx.resolution_index);
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

void Game::build_class_select_ui(UIScene& ui) {
    float center_x = screen_width() / 2.0f;
    float center_y = screen_height() / 2.0f;
    int num_classes = static_cast<int>(available_classes_.size());
    if (num_classes == 0) return;

    ui.add_filled_rect(0, 0, static_cast<float>(screen_width()), 100, ui_colors::TITLE_BG);
    // Center title text based on estimated character width (~8px per char)
    const char* title = "SELECT YOUR CLASS";
    float title_width = static_cast<float>(strlen(title)) * 8.0f * 2.0f;
    ui.add_text(title, center_x - title_width / 2.0f, 30.0f, 2.0f, ui_colors::WHITE);
    const char* subtitle = "Use A/D to select, SPACE to confirm";
    float subtitle_width = static_cast<float>(strlen(subtitle)) * 8.0f * 1.0f;
    ui.add_text(subtitle, center_x - subtitle_width / 2.0f, 70.0f, 1.0f, ui_colors::TEXT_DIM);

    float box_size = 120.0f;
    float spacing = 150.0f;
    float start_x = center_x - spacing * (num_classes - 1) / 2.0f;
    float box_y = center_y - 50.0f;

    for (int i = 0; i < num_classes; ++i) {
        const auto& cls = available_classes_[i];
        float x = start_x + i * spacing;
        bool selected = (i == selected_class_index_);

        if (selected) {
            ui.add_filled_rect(x - box_size/2 - 10, box_y - box_size/2 - 10,
                              box_size + 20, box_size + 20, ui_colors::SELECTION);
            ui.add_rect_outline(x - box_size/2 - 10, box_y - box_size/2 - 10,
                               box_size + 20, box_size + 20, ui_colors::WHITE, 3.0f);
        }

        ui.add_filled_rect(x - box_size/2, box_y - box_size/2, box_size, box_size, cls.select_color);

        float half = box_size / 2.0f;
        ui.add_filled_rect(x - half, box_y - half, box_size, box_size, cls.color);
        ui.add_rect_outline(x - half, box_y - half, box_size, box_size, ui_colors::WHITE, 2.0f);

        uint32_t text_color = selected ? ui_colors::WHITE : ui_colors::TEXT_DIM;
        // Center text based on estimated character width (~8px at scale 1.0)
        float name_width = static_cast<float>(strlen(cls.name)) * 8.0f * 1.0f;
        float desc_width = static_cast<float>(strlen(cls.short_desc)) * 8.0f * 0.8f;
        ui.add_text(cls.name, x - name_width / 2.0f, box_y + box_size/2 + 15.0f, 1.0f, text_color);
        ui.add_text(cls.short_desc, x - desc_width / 2.0f, box_y + box_size/2 + 40.0f, 0.8f, ui_colors::TEXT_DIM);
    }

    const auto& sel = available_classes_[selected_class_index_];
    ui.add_filled_rect(center_x - 200, screen_height() - 120, 400, 80, ui_colors::PANEL_BG);
    ui.add_rect_outline(center_x - 200, screen_height() - 120, 400, 80, sel.select_color, 2.0f);

    // Center description lines based on estimated character width
    float desc1_width = static_cast<float>(strlen(sel.desc_line1)) * 8.0f * 0.9f;
    float desc2_width = static_cast<float>(strlen(sel.desc_line2)) * 8.0f * 0.9f;
    ui.add_text(sel.desc_line1, center_x - desc1_width / 2.0f, screen_height() - 105.0f, 0.9f, ui_colors::WHITE);
    ui.add_text(sel.desc_line2, center_x - desc2_width / 2.0f, screen_height() - 80.0f, 0.9f, ui_colors::WHITE);

    const char* hint = "Press ESC anytime to open Settings Menu";
    float hint_width = static_cast<float>(strlen(hint)) * 8.0f * 0.8f;
    ui.add_text(hint, center_x - hint_width / 2.0f, screen_height() - 25.0f, 0.8f, ui_colors::TEXT_HINT);
}

void Game::build_connecting_ui(UIScene& ui) {
    float center_x = screen_width() / 2.0f;
    float center_y = screen_height() / 2.0f;

    ui.add_filled_rect(center_x - 200, center_y - 100, 400, 200, ui_colors::PANEL_BG);
    ui.add_rect_outline(center_x - 200, center_y - 100, 400, 200, ui_colors::WHITE, 2.0f);

    ui.add_text("CONNECTING", center_x - 80.0f, center_y - 80.0f, 1.5f, ui_colors::WHITE);

    int num_dots = 8;
    float radius = 40.0f;
    float dot_radius = 8.0f;
    float angle_offset = connecting_timer_ * 3.0f;

    for (int i = 0; i < num_dots; ++i) {
        float angle = angle_offset + (i / static_cast<float>(num_dots)) * 2.0f * 3.14159f;
        float x = center_x + std::cos(angle) * radius;
        float y = center_y + std::sin(angle) * radius;
        uint8_t alpha = static_cast<uint8_t>(255 * (i + 1) / static_cast<float>(num_dots));
        ui.add_filled_rect(x - dot_radius, y - dot_radius,
                          dot_radius * 2, dot_radius * 2,
                          0x00FFFFFF | (alpha << 24));
    }

    std::string connect_msg = "Connecting to " + host_ + ":" + std::to_string(port_);
    ui.add_text(connect_msg, center_x - 120.0f, center_y + 60.0f, 0.8f, ui_colors::TEXT_DIM);
}

void Game::build_playing_ui(UIScene& ui) {
    if (selected_class_index_ >= 0 && selected_class_index_ < static_cast<int>(available_classes_.size()) &&
        available_classes_[selected_class_index_].shows_reticle) {
        float cx = screen_width() / 2.0f;
        float cy = screen_height() / 2.0f;
        float outer = 12.0f, inner = 4.0f, dot = 2.0f;
        ui.add_line(cx, cy - outer, cx, cy - inner, 0xCCFFFFFF, 2.0f);
        ui.add_line(cx, cy + inner, cx, cy + outer, 0xCCFFFFFF, 2.0f);
        ui.add_line(cx - outer, cy, cx - inner, cy, 0xCCFFFFFF, 2.0f);
        ui.add_line(cx + inner, cy, cx + outer, cy, 0xCCFFFFFF, 2.0f);
        ui.add_filled_rect(cx - dot / 2, cy - dot / 2, dot, dot, 0xCCFFFFFF);
    }

    auto it = network_to_entity_.find(local_player_id_);
    if (it != network_to_entity_.end() && registry_.valid(it->second)) {
        auto& health = registry_.get<ecs::Health>(it->second);
        float health_ratio = health.current / health.max;

        float bar_width = 250.0f;
        float bar_height = 25.0f;
        float padding = 20.0f;
        float hx = padding;
        float hy = screen_height() - padding - bar_height;

        ui.add_filled_rect(hx - 2, hy - 2, bar_width + 4, bar_height + 4, ui_colors::HEALTH_FRAME);
        ui.add_rect_outline(hx - 2, hy - 2, bar_width + 4, bar_height + 4, ui_colors::BORDER, 2.0f);
        ui.add_filled_rect(hx, hy, bar_width, bar_height, ui_colors::HEALTH_BG);

        uint32_t hp_color;
        if (health_ratio > 0.5f) hp_color = ui_colors::HEALTH_HIGH;
        else if (health_ratio > 0.25f) hp_color = ui_colors::HEALTH_MED;
        else hp_color = ui_colors::HEALTH_LOW;
        ui.add_filled_rect(hx, hy, bar_width * health_ratio, bar_height, hp_color);

        char hp_text[32];
        snprintf(hp_text, sizeof(hp_text), "HP: %.0f / %.0f", health_ratio * health.max, health.max);
        ui.add_text(hp_text, hx + 10, hy + 5, 1.0f, ui_colors::WHITE);
    }

    if (menu_system_->graphics_settings().show_fps) {
        char fps_text[32];
        snprintf(fps_text, sizeof(fps_text), "FPS: %.0f", fps());
        ui.add_text(fps_text, 10.0f, 10.0f, 1.0f, ui_colors::FPS_TEXT);
    }

    if (menu_system_->graphics_settings().show_debug_hud) {
        auto& rs = render_stats();
        auto& ns = network_.network_stats();
        float x = 10.0f;
        float y = menu_system_->graphics_settings().show_fps ? 30.0f : 10.0f;
        float line_h = 18.0f;
        uint32_t color = ui_colors::FPS_TEXT;

        ui.add_filled_rect(x - 5, y - 5, 420, 5 * line_h + 10, 0x80000000);

        char buf[128];
        snprintf(buf, sizeof(buf), "GPU: %s %dx%d | Draws: %u | Tris: %uK",
                 gpu_driver_name().c_str(), screen_width(), screen_height(),
                 rs.draw_calls, rs.triangle_count / 1000);
        ui.add_text(buf, x, y, 0.8f, color);
        y += line_h;

        snprintf(buf, sizeof(buf), "Entities: %u rendered, %u dist culled, %u frustum culled",
                 rs.entities_rendered, rs.entities_distance_culled, rs.entities_frustum_culled);
        ui.add_text(buf, x, y, 0.8f, color);
        y += line_h;

        snprintf(buf, sizeof(buf), "Frame: %.1f ms (%.0f FPS)", 1000.0f / fps(), fps());
        ui.add_text(buf, x, y, 0.8f, color);
        y += line_h;

        snprintf(buf, sizeof(buf), "Net: %.1f KB/s up, %.1f KB/s down",
                 ns.bytes_sent_per_sec / 1024.0f, ns.bytes_recv_per_sec / 1024.0f);
        ui.add_text(buf, x, y, 0.8f, color);
        y += line_h;

        snprintf(buf, sizeof(buf), "Net: %.0f pkt/s up, %.0f pkt/s down, queue: %u",
                 ns.packets_sent_per_sec, ns.packets_recv_per_sec, ns.message_queue_size);
        ui.add_text(buf, x, y, 0.8f, color);
    }

    // Quest markers above NPCs (only show for NPCs with available/completable quests)
    {
        auto npc_view = registry_.view<ecs::NetworkId, ecs::Transform, ecs::EntityInfo>();
        for (auto entity : npc_view) {
            auto& info = npc_view.get<ecs::EntityInfo>(entity);
            if (info.type != EntityType::TownNPC) continue;

            auto& net_id = npc_view.get<ecs::NetworkId>(entity);
            bool has_quest = npcs_with_quests_.count(net_id.id) > 0;
            bool has_turnin = npcs_with_turnins_.count(net_id.id) > 0;
            if (!has_quest && !has_turnin) continue;

            auto& transform = npc_view.get<ecs::Transform>(entity);

            // Project world position to screen
            float marker_world_y = transform.y + info.target_size * 1.6f;
            glm::vec4 world_pos(transform.x, marker_world_y, transform.z, 1.0f);
            glm::vec4 clip = cached_vp_matrix_ * world_pos;
            if (clip.w <= 0.0f) continue;

            float ndc_x = clip.x / clip.w;
            float ndc_y = clip.y / clip.w;
            float sx = (ndc_x * 0.5f + 0.5f) * cached_screen_w_;
            float sy = (1.0f - (ndc_y * 0.5f + 0.5f)) * cached_screen_h_;

            if (sx < -50 || sx > cached_screen_w_ + 50 || sy < -50 || sy > cached_screen_h_ + 50) continue;

            float dx = transform.x - player_x_;
            float dz = transform.z - player_z_;
            float dist_sq = dx * dx + dz * dz;
            if (dist_sq > 800.0f * 800.0f) continue;

            // Scale marker based on distance
            float dist = std::sqrt(dist_sq);
            float scale_factor = std::max(0.6f, 1.0f - dist / 1000.0f);
            float circle_r = 14.0f * scale_factor;

            if (has_turnin) {
                // Yellow "?" - quest ready to turn in
                ui.add_filled_rect(sx - circle_r, sy - circle_r, circle_r * 2, circle_r * 2, 0xCC002200);
                ui.add_rect_outline(sx - circle_r, sy - circle_r, circle_r * 2, circle_r * 2, 0xFF00FF00, 2.0f);
                ui.add_text("?", sx - 5 * scale_factor, sy - 10 * scale_factor, 1.4f * scale_factor, 0xFF00FF00);
            } else {
                // Gold "!" - quest available
                ui.add_filled_rect(sx - circle_r, sy - circle_r, circle_r * 2, circle_r * 2, 0xCC003344);
                ui.add_rect_outline(sx - circle_r, sy - circle_r, circle_r * 2, circle_r * 2, 0xFF00DDFF, 2.0f);
                ui.add_text("!", sx - 4 * scale_factor, sy - 10 * scale_factor, 1.4f * scale_factor, 0xFF00DDFF);
            }
        }
    }

    // NPC Quest dialogue popup
    if (npc_interaction_.showing_dialogue) {
        float pw = 500.0f, ph = 400.0f;
        float px = (screen_width() - pw) / 2.0f;
        float py = (screen_height() - ph) / 2.0f;

        // Background panel
        ui.add_filled_rect(px, py, pw, ph, 0xEE1a1a2e);
        ui.add_rect_outline(px, py, pw, ph, 0xFF00BBFF, 2.0f);

        // NPC name header
        ui.add_filled_rect(px, py, pw, 35.0f, 0xFF332211);
        ui.add_text(npc_interaction_.npc_name, px + 15, py + 8, 1.2f, 0xFF00DDFF);

        // Close hint
        ui.add_text("[ESC] Close", px + pw - 120, py + 10, 0.8f, 0xFF888888);

        if (!npc_interaction_.showing_quest_detail) {
            // Quest list view
            if (npc_interaction_.available_quests.empty()) {
                ui.add_text("No quests available.", px + 20, py + 60, 1.0f, 0xFF888888);
            } else {
                ui.add_text("Available Quests:", px + 20, py + 50, 1.0f, 0xFFCCCCCC);

                float qy = py + 80;
                for (int i = 0; i < static_cast<int>(npc_interaction_.available_quests.size()); ++i) {
                    auto& quest = npc_interaction_.available_quests[i];
                    bool selected = (i == npc_interaction_.selected_quest);

                    // Quest entry background
                    if (selected) {
                        ui.add_filled_rect(px + 10, qy - 2, pw - 20, 28.0f, 0x40FFFFFF);
                    }

                    // Quest name
                    ui.add_text(quest.quest_name, px + 25, qy + 2, 1.0f, selected ? 0xFFFFFFFF : 0xFFCCCCCC);

                    // Rewards preview
                    char reward_text[64];
                    snprintf(reward_text, sizeof(reward_text), "XP: %d  Gold: %d", quest.xp_reward, quest.gold_reward);
                    ui.add_text(reward_text, px + pw - 180, qy + 4, 0.7f, 0xFF00DDFF);

                    qy += 32.0f;
                }

                ui.add_text("[W/S] Navigate  [ENTER] View Quest", px + 20, py + ph - 35, 0.8f, 0xFF888888);
            }
        } else {
            // Quest detail view
            auto& quest = npc_interaction_.available_quests[npc_interaction_.selected_quest];

            // Quest name
            ui.add_text(quest.quest_name, px + 20, py + 50, 1.2f, 0xFF00DDFF);

            // Dialogue
            ui.add_text(quest.dialogue, px + 20, py + 80, 0.8f, 0xFFCCCCCC);

            // Description
            ui.add_text(quest.description, px + 20, py + 130, 0.85f, 0xFFAAAAAA);

            // Objectives
            ui.add_text("Objectives:", px + 20, py + 170, 1.0f, 0xFFFFFFFF);
            float oy = py + 195;
            for (auto& obj : quest.objectives) {
                char obj_text[128];
                snprintf(obj_text, sizeof(obj_text), "- %s (%d)", obj.description.c_str(), obj.count);
                ui.add_text(obj_text, px + 30, oy, 0.85f, 0xFFCCCCCC);
                oy += 22.0f;
            }

            // Rewards
            oy += 10.0f;
            ui.add_text("Rewards:", px + 20, oy, 1.0f, 0xFFFFFFFF);
            oy += 25.0f;
            char reward_buf[64];
            snprintf(reward_buf, sizeof(reward_buf), "XP: %d   Gold: %d", quest.xp_reward, quest.gold_reward);
            ui.add_text(reward_buf, px + 30, oy, 0.9f, 0xFF00DDFF);

            // Accept / Decline buttons
            float btn_y = py + ph - 50;
            ui.add_filled_rect(px + pw / 2 - 160, btn_y, 140, 35, 0xFF004400);
            ui.add_rect_outline(px + pw / 2 - 160, btn_y, 140, 35, 0xFF00CC00, 2.0f);
            ui.add_text("Accept [E]", px + pw / 2 - 130, btn_y + 8, 1.0f, 0xFF00FF00);

            ui.add_filled_rect(px + pw / 2 + 20, btn_y, 140, 35, 0xFF440000);
            ui.add_rect_outline(px + pw / 2 + 20, btn_y, 140, 35, 0xFFCC0000, 2.0f);
            ui.add_text("Decline [Q]", px + pw / 2 + 45, btn_y + 8, 1.0f, 0xFFFF4444);
        }
    }

    // Gameplay HUD overlay
    build_gameplay_hud(ui, hud_state_, static_cast<float>(screen_width()), static_cast<float>(screen_height()));
    build_gameplay_panels(ui, panel_state_, static_cast<float>(screen_width()), static_cast<float>(screen_height()));

    // Gameplay UI layers (client branch)
    build_skill_bar_ui(ui);
    build_quest_tracker_ui(ui);
    build_notifications_ui(ui);
    build_damage_numbers_ui(ui);
    build_dialogue_ui(ui);

    // Panels (drawn on top)
    switch (panel_state_.active_panel) {
        case ActivePanel::Inventory:
            build_inventory_panel_ui(ui);
            break;
        case ActivePanel::Talents:
            build_talent_panel_ui(ui);
            break;
        case ActivePanel::QuestLog:
            build_quest_log_panel_ui(ui);
            break;
        case ActivePanel::None:
            break;
    }

    // Death overlay
    if (player_dead_) {
        ui.add_filled_rect(0, 0, static_cast<float>(screen_width()),
                           static_cast<float>(screen_height()), 0x88000000);
        float cx = screen_width() / 2.0f;
        float cy = screen_height() / 2.0f;
        ui.add_text("YOU DIED", cx - 64.0f, cy - 20.0f, 2.0f, 0xFF4444FF);
        ui.add_text("Press SPACE to respawn", cx - 100.0f, cy + 30.0f, 1.0f, ui_colors::TEXT_DIM);
    }
}

// ============================================================================
// Delta Compression Handlers
// ============================================================================

void Game::on_entity_enter(const std::vector<uint8_t>& payload) {
    if (payload.size() < NetEntityState::serialized_size()) return;

    // Deserialize full entity state
    NetEntityState state;
    state.deserialize(payload);

    // Create or find entity
    entt::entity entity = find_or_create_entity(state.id);
    update_entity_from_state(entity, state);

    // Track attack state for spawning attack effects
    prev_attacking_[state.id] = state.is_attacking;
}

void Game::on_entity_update(const std::vector<uint8_t>& payload) {
    if (payload.size() < sizeof(uint32_t) + sizeof(uint8_t)) return;

    // Deserialize delta
    mmo::protocol::EntityDeltaUpdate delta;
    delta.deserialize(payload);

    // Find entity
    auto it = network_to_entity_.find(delta.id);
    if (it == network_to_entity_.end()) {
        return;  // Unknown entity (shouldn't happen)
    }

    entt::entity entity = it->second;
    if (!registry_.valid(entity)) return;

    // Apply delta to entity components
    apply_delta_to_entity(entity, delta);
}

void Game::on_entity_exit(const std::vector<uint8_t>& payload) {
    if (payload.size() >= EntityExitMsg::serialized_size()) {
        EntityExitMsg msg;
        msg.deserialize(payload);

        remove_entity(msg.entity_id);
        prev_attacking_.erase(msg.entity_id);
    }
}

void Game::apply_delta_to_entity(entt::entity entity, const mmo::protocol::EntityDeltaUpdate& delta) {
    // Update transform (position)
    if (delta.flags & mmo::protocol::EntityDeltaUpdate::FLAG_POSITION) {
        if (registry_.all_of<ecs::Transform, ecs::Interpolation>(entity)) {
            auto& transform = registry_.get<ecs::Transform>(entity);
            auto& interp = registry_.get<ecs::Interpolation>(entity);

            interp.prev_x = transform.x;
            interp.prev_y = transform.y;
            interp.prev_z = transform.z;
            interp.target_x = delta.x;
            interp.target_y = delta.y;
            interp.target_z = delta.z;
            interp.alpha = 0.0f;
        }
    }

    // Update velocity
    if (delta.flags & mmo::protocol::EntityDeltaUpdate::FLAG_VELOCITY) {
        if (registry_.all_of<ecs::Velocity>(entity)) {
            auto& velocity = registry_.get<ecs::Velocity>(entity);
            velocity.x = delta.vx;
            velocity.z = delta.vy;
        }
    }

    // Update health
    if (delta.flags & mmo::protocol::EntityDeltaUpdate::FLAG_HEALTH) {
        if (registry_.all_of<ecs::Health>(entity)) {
            auto& health = registry_.get<ecs::Health>(entity);
            health.current = delta.health;
        }
    }

    // Update attacking state
    if (delta.flags & mmo::protocol::EntityDeltaUpdate::FLAG_ATTACKING) {
        if (registry_.all_of<ecs::Combat>(entity)) {
            auto& combat = registry_.get<ecs::Combat>(entity);
            bool was_attacking = combat.is_attacking;
            combat.is_attacking = (delta.is_attacking != 0);

            // Set cooldown so the attack tilt animation plays
            if (combat.is_attacking) {
                combat.current_cooldown = 0.5f;
            } else {
                combat.current_cooldown = 0.0f;
            }

            // Spawn attack effect if transitioning to attacking
            if (combat.is_attacking && !was_attacking) {
                if (registry_.all_of<ecs::NetworkId, ecs::Transform, ecs::EntityInfo>(entity)) {
                    uint32_t net_id = registry_.get<ecs::NetworkId>(entity).id;
                    prev_attacking_[net_id] = true;

                    // Get attack direction
                    float dir_x = 0.0f;
                    float dir_y = 1.0f;
                    if (delta.flags & mmo::protocol::EntityDeltaUpdate::FLAG_ATTACK_DIR) {
                        dir_x = delta.attack_dir_x;
                        dir_y = delta.attack_dir_y;
                    } else if (registry_.all_of<ecs::AttackDirection>(entity)) {
                        auto& dir = registry_.get<ecs::AttackDirection>(entity);
                        dir_x = dir.x;
                        dir_y = dir.y;
                    }

                    float len = std::sqrt(dir_x * dir_x + dir_y * dir_y);
                    if (len < 0.001f) {
                        dir_x = 0; dir_y = 1;
                    } else {
                        dir_x /= len; dir_y /= len;
                    }

                    // Build state from cached entity components for effect spawning
                    auto& transform = registry_.get<ecs::Transform>(entity);
                    auto& info = registry_.get<ecs::EntityInfo>(entity);
                    NetEntityState state;
                    state.id = net_id;
                    state.x = transform.x;
                    state.z = transform.z;
                    std::strncpy(state.effect_type, info.effect_type.c_str(), 15);
                    state.effect_type[15] = '\0';
                    state.cone_angle = info.cone_angle;

                    spawn_attack_effect(state, dir_x, dir_y);

                    if (net_id == local_player_id_) {
                        camera().notify_attack();
                    }
                }
            }
        }
    }

    // Update attack direction
    if (delta.flags & mmo::protocol::EntityDeltaUpdate::FLAG_ATTACK_DIR) {
        if (!registry_.all_of<ecs::AttackDirection>(entity)) {
            registry_.emplace<ecs::AttackDirection>(entity);
        }
        auto& attack_dir = registry_.get<ecs::AttackDirection>(entity);
        attack_dir.x = delta.attack_dir_x;
        attack_dir.y = delta.attack_dir_y;
    }

    // Update rotation
    if (delta.flags & mmo::protocol::EntityDeltaUpdate::FLAG_ROTATION) {
        if (registry_.all_of<ecs::Transform>(entity)) {
            auto& transform = registry_.get<ecs::Transform>(entity);
            transform.rotation = delta.rotation;
        }
    }
}

// ============================================================================
// Gameplay Message Handlers
// ============================================================================

void Game::on_combat_event(const std::vector<uint8_t>& payload) {
    if (payload.size() < CombatEventMsg::serialized_size()) return;

    CombatEventMsg msg;
    msg.deserialize(payload);

    // Add floating damage number
    DamageNumber dn;
    dn.x = msg.target_x;
    dn.y = msg.target_y + 30.0f;  // Offset above target
    dn.z = msg.target_z;
    dn.damage = msg.damage;
    dn.timer = DamageNumber::DURATION;
    dn.is_heal = (msg.is_heal != 0);
    hud_state_.damage_numbers.push_back(dn);
}

void Game::on_entity_death(const std::vector<uint8_t>& payload) {
    if (payload.size() < EntityDeathMsg::serialized_size()) return;

    EntityDeathMsg msg;
    msg.deserialize(payload);

    // Check if local player died
    if (msg.entity_id == local_player_id_) {
        player_dead_ = true;
    }
}

void Game::on_quest_progress(const std::vector<uint8_t>& payload) {
    if (payload.size() < QuestProgressMsg::serialized_size()) return;

    QuestProgressMsg msg;
    msg.deserialize(payload);

    // Find matching quest and update objective
    std::string qid(msg.quest_id, strnlen(msg.quest_id, sizeof(msg.quest_id)));
    for (auto& quest : hud_state_.tracked_quests) {
        if (quest.quest_id == qid) {
            if (msg.objective_index < quest.objectives.size()) {
                quest.objectives[msg.objective_index].current = msg.current;
                quest.objectives[msg.objective_index].required = msg.required;
                quest.objectives[msg.objective_index].complete = (msg.complete != 0);
            }
            break;
        }
    }
}

void Game::on_quest_complete(const std::vector<uint8_t>& payload) {
    if (payload.size() < QuestCompleteMsg::serialized_size()) return;

    QuestCompleteMsg msg;
    msg.deserialize(payload);

    // Remove from tracked quests
    std::string qid(msg.quest_id, strnlen(msg.quest_id, sizeof(msg.quest_id)));
    auto& quests = hud_state_.tracked_quests;
    quests.erase(
        std::remove_if(quests.begin(), quests.end(),
            [&](const QuestTrackerEntry& q) { return q.quest_id == qid; }),
        quests.end());

    // Show completion notification
    Notification notif;
    notif.text = std::string("Quest Complete: ") + msg.quest_name;
    notif.timer = Notification::DURATION;
    notif.color = 0xFF00FFFF;  // Yellow
    hud_state_.notifications.push_back(notif);
}

void Game::on_inventory_update(const std::vector<uint8_t>& payload) {
    if (payload.size() < InventoryUpdateMsg::serialized_size()) return;

    InventoryUpdateMsg msg;
    msg.deserialize(payload);

    for (int i = 0; i < PanelState::MAX_INVENTORY_SLOTS; ++i) {
        panel_state_.inventory_slots[i].item_id = msg.slots[i].item_id;
        panel_state_.inventory_slots[i].count = msg.slots[i].count;
    }
    panel_state_.equipped_weapon = msg.equipped_weapon;
    panel_state_.equipped_armor = msg.equipped_armor;
}

void Game::on_skill_cooldown(const std::vector<uint8_t>& payload) {
    if (payload.size() < SkillCooldownMsg::serialized_size()) return;

    SkillCooldownMsg msg;
    msg.deserialize(payload);

    // Find matching skill slot and override cooldown
    // SkillCooldownMsg has uint16_t skill_id - match by index
    for (int i = 0; i < 5; ++i) {
        if (i == msg.skill_id) {
            hud_state_.skill_slots[i].cooldown = msg.cooldown_remaining;
            hud_state_.skill_slots[i].max_cooldown = msg.cooldown_total;
            break;
        }
    }
}

void Game::on_skill_unlock(const std::vector<uint8_t>& payload) {
    // Server sends one SkillResultMsg per unlocked skill via SkillUnlock message type
    if (payload.size() < SkillResultMsg::serialized_size()) return;

    SkillResultMsg msg;
    msg.deserialize(payload);

    std::string sid(msg.skill_id, strnlen(msg.skill_id, sizeof(msg.skill_id)));

    // Find existing slot or first empty
    for (int i = 0; i < 5; ++i) {
        if (hud_state_.skill_slots[i].skill_id == sid) {
            hud_state_.skill_slots[i].max_cooldown = msg.cooldown;
            return;
        }
    }
    for (int i = 0; i < 5; ++i) {
        if (!hud_state_.skill_slots[i].available) {
            hud_state_.skill_slots[i].skill_id = sid;
            hud_state_.skill_slots[i].name = sid;
            hud_state_.skill_slots[i].max_cooldown = msg.cooldown;
            hud_state_.skill_slots[i].available = true;
            hud_state_.skill_slots[i].key_number = i + 1;
            return;
        }
    }
}

void Game::on_talent_sync(const std::vector<uint8_t>& payload) {
    if (payload.size() < TalentSyncMsg::serialized_size()) return;

    TalentSyncMsg msg;
    msg.deserialize(payload);

    panel_state_.talent_points = msg.talent_points;
    panel_state_.talent_points_display = msg.talent_points;
    panel_state_.unlocked_talents.clear();
    for (int i = 0; i < msg.unlocked_count && i < TalentSyncMsg::MAX_TALENTS; ++i) {
        // unlocked_ids is char[32] array - convert to uint16_t hash for panel tracking
        panel_state_.unlocked_talents.push_back(static_cast<uint16_t>(i + 1));
    }
}

void Game::on_npc_dialogue(const std::vector<uint8_t>& payload) {
    if (payload.size() < NPCDialogueMsg::serialized_size()) return;

    NPCDialogueMsg msg;
    msg.deserialize(payload);

    auto& dlg = hud_state_.dialogue;
    dlg.visible = true;
    dlg.npc_id = msg.npc_id;
    dlg.npc_name = msg.npc_name;
    dlg.dialogue = msg.dialogue;
    dlg.quest_count = msg.quest_count;
    dlg.selected_option = 0;
    for (int i = 0; i < 4; ++i) {
        dlg.quest_ids[i] = msg.quest_ids[i];
        dlg.quest_names[i] = msg.quest_names[i];
    }
}

// ============================================================================
// Panel Interaction
// ============================================================================

void Game::update_panel_input(float /*dt*/) {
    const bool* keys = SDL_GetKeyboardState(nullptr);

    // Panel toggle keys (only on key-down edge)
    static bool i_was_down = false;
    static bool t_was_down = false;
    static bool l_was_down = false;
    static bool e_was_down = false;
    static bool esc_was_down = false;
    static bool space_was_down = false;

    bool i_down = keys[SDL_SCANCODE_I];
    bool t_down = keys[SDL_SCANCODE_T];
    bool l_down = keys[SDL_SCANCODE_L];
    bool e_down = keys[SDL_SCANCODE_E];
    bool esc_down = keys[SDL_SCANCODE_ESCAPE];
    bool space_down = keys[SDL_SCANCODE_SPACE];

    // Handle death respawn
    if (player_dead_ && space_down && !space_was_down) {
        player_dead_ = false;
        // Respawn is handled server-side; just clear the overlay
    }

    // Close dialogue on ESC or E
    if (hud_state_.dialogue.visible) {
        if ((esc_down && !esc_was_down) || (e_down && !e_was_down)) {
            hud_state_.dialogue.visible = false;
        }

        // Navigate dialogue options
        if (hud_state_.dialogue.quest_count > 0) {
            if (keys[SDL_SCANCODE_UP] || keys[SDL_SCANCODE_W]) {
                // Handled per-frame but fine for simple nav
            }
            if (keys[SDL_SCANCODE_DOWN] || keys[SDL_SCANCODE_S]) {
            }

            // Accept quest on Enter/Space
            if ((keys[SDL_SCANCODE_RETURN] || (space_down && !space_was_down)) &&
                hud_state_.dialogue.selected_option < hud_state_.dialogue.quest_count) {
                uint16_t qid = hud_state_.dialogue.quest_ids[hud_state_.dialogue.selected_option];
                if (qid > 0) {
                    // QuestAcceptMsg uses the same quest_id format as QuestOffer
                    // For now send the quest name as the ID
                    QuestAcceptMsg msg;
                    std::strncpy(msg.quest_id,
                        hud_state_.dialogue.quest_names[hud_state_.dialogue.selected_option].c_str(),
                        sizeof(msg.quest_id) - 1);
                    network_.send_raw(build_packet(MessageType::QuestAccept, msg));
                }
                hud_state_.dialogue.visible = false;
            }
        }

        // Dialogue consumes input, skip panel toggles
        e_was_down = e_down;
        i_was_down = i_down;
        t_was_down = t_down;
        l_was_down = l_down;
        esc_was_down = esc_down;
        space_was_down = space_down;
        return;
    }

    // Toggle panels
    if (i_down && !i_was_down) panel_state_.toggle_panel(ActivePanel::Inventory);
    if (t_down && !t_was_down) panel_state_.toggle_panel(ActivePanel::Talents);
    if (l_down && !l_was_down) panel_state_.toggle_panel(ActivePanel::QuestLog);

    // Close panel on ESC
    if (esc_down && !esc_was_down && panel_state_.is_panel_open()) {
        panel_state_.active_panel = ActivePanel::None;
    }

    // Panel-specific interaction
    if (panel_state_.active_panel == ActivePanel::Inventory) {
        // Navigate inventory with arrow keys
        static bool up_was = false, down_was = false;
        bool up_now = keys[SDL_SCANCODE_UP] || keys[SDL_SCANCODE_W];
        bool down_now = keys[SDL_SCANCODE_DOWN] || keys[SDL_SCANCODE_S];

        if (up_now && !up_was && panel_state_.inventory_cursor > 0) {
            panel_state_.inventory_cursor--;
        }
        if (down_now && !down_was && panel_state_.inventory_cursor < PanelState::MAX_INVENTORY_SLOTS - 1) {
            panel_state_.inventory_cursor++;
        }
        up_was = up_now;
        down_was = down_now;

        // Equip item on Enter
        static bool enter_was = false;
        bool enter_now = keys[SDL_SCANCODE_RETURN];
        if (enter_now && !enter_was) {
            auto& slot = panel_state_.inventory_slots[panel_state_.inventory_cursor];
            if (!slot.empty()) {
                ItemEquipMsg msg;
                msg.slot_index = static_cast<uint8_t>(panel_state_.inventory_cursor);
                network_.send_raw(build_packet(MessageType::ItemEquip, msg));
            }
        }
        enter_was = enter_now;

        // Unequip weapon on 1, armor on 2
        static bool key1_was = false, key2_was = false;
        bool key1_now = keys[SDL_SCANCODE_1];
        bool key2_now = keys[SDL_SCANCODE_2];
        if (key1_now && !key1_was && panel_state_.equipped_weapon > 0) {
            ItemUnequipMsg msg;
            msg.equip_slot = 0;
            network_.send_raw(build_packet(MessageType::ItemUnequip, msg));
        }
        if (key2_now && !key2_was && panel_state_.equipped_armor > 0) {
            ItemUnequipMsg msg;
            msg.equip_slot = 1;
            network_.send_raw(build_packet(MessageType::ItemUnequip, msg));
        }
        key1_was = key1_now;
        key2_was = key2_now;

        // Use consumable on U
        static bool u_was = false;
        bool u_now = keys[SDL_SCANCODE_U];
        if (u_now && !u_was) {
            auto& slot = panel_state_.inventory_slots[panel_state_.inventory_cursor];
            if (!slot.empty()) {
                ItemUseMsg msg;
                msg.slot_index = static_cast<uint8_t>(panel_state_.inventory_cursor);
                network_.send_raw(build_packet(MessageType::ItemUse, msg));
            }
        }
        u_was = u_now;
    }

    if (panel_state_.active_panel == ActivePanel::Talents) {
        static bool up_was = false, down_was = false, enter_was = false;
        bool up_now = keys[SDL_SCANCODE_UP] || keys[SDL_SCANCODE_W];
        bool down_now = keys[SDL_SCANCODE_DOWN] || keys[SDL_SCANCODE_S];
        bool enter_now = keys[SDL_SCANCODE_RETURN];

        if (up_now && !up_was && panel_state_.talent_cursor > 0) {
            panel_state_.talent_cursor--;
        }
        if (down_now && !down_was) {
            panel_state_.talent_cursor++;
        }
        up_was = up_now;
        down_was = down_now;

        // Unlock talent on Enter if we have points
        if (enter_now && !enter_was && panel_state_.talent_points > 0) {
            // Send talent unlock for the currently selected talent
            // The talent_cursor maps to talent IDs starting at 1
            int tid = panel_state_.talent_cursor + 1;
            // Only unlock if not already unlocked
            bool already_unlocked = false;
            for (uint16_t ut : panel_state_.unlocked_talents) {
                if (ut == static_cast<uint16_t>(tid)) { already_unlocked = true; break; }
            }
            if (!already_unlocked) {
                TalentUnlockMsg msg;
                snprintf(msg.talent_id, sizeof(msg.talent_id), "talent_%d", tid);
                network_.send_raw(build_packet(MessageType::TalentUnlock, msg));
            }
        }
        enter_was = enter_now;
    }

    if (panel_state_.active_panel == ActivePanel::QuestLog) {
        static bool up_was = false, down_was = false, del_was = false;
        bool up_now = keys[SDL_SCANCODE_UP] || keys[SDL_SCANCODE_W];
        bool down_now = keys[SDL_SCANCODE_DOWN] || keys[SDL_SCANCODE_S];
        bool del_now = keys[SDL_SCANCODE_DELETE] || keys[SDL_SCANCODE_X];

        if (up_now && !up_was && panel_state_.quest_cursor > 0) {
            panel_state_.quest_cursor--;
        }
        if (down_now && !down_was &&
            panel_state_.quest_cursor < static_cast<int>(hud_state_.tracked_quests.size()) - 1) {
            panel_state_.quest_cursor++;
        }
        up_was = up_now;
        down_was = down_now;

        // Abandon quest on Delete/X (local removal)
        if (del_now && !del_was &&
            panel_state_.quest_cursor < static_cast<int>(hud_state_.tracked_quests.size())) {
            hud_state_.tracked_quests.erase(
                hud_state_.tracked_quests.begin() + panel_state_.quest_cursor);
            if (panel_state_.quest_cursor > 0 &&
                panel_state_.quest_cursor >= static_cast<int>(hud_state_.tracked_quests.size())) {
                panel_state_.quest_cursor--;
            }
        }
        del_was = del_now;
    }

    i_was_down = i_down;
    t_was_down = t_down;
    l_was_down = l_down;
    e_was_down = e_down;
    esc_was_down = esc_down;
    space_was_down = space_down;
}

void Game::update_damage_numbers(float dt) {
    for (auto& dn : hud_state_.damage_numbers) {
        dn.timer -= dt;
        dn.y += 40.0f * dt;  // Float upward
    }
    // Remove expired
    hud_state_.damage_numbers.erase(
        std::remove_if(hud_state_.damage_numbers.begin(), hud_state_.damage_numbers.end(),
            [](const DamageNumber& d) { return d.timer <= 0.0f; }),
        hud_state_.damage_numbers.end());
}

void Game::update_notifications(float dt) {
    for (auto& n : hud_state_.notifications) {
        n.timer -= dt;
    }
    hud_state_.notifications.erase(
        std::remove_if(hud_state_.notifications.begin(), hud_state_.notifications.end(),
            [](const Notification& n) { return n.timer <= 0.0f; }),
        hud_state_.notifications.end());
}

// ============================================================================
// Gameplay UI Rendering
// ============================================================================

void Game::build_skill_bar_ui(UIScene& ui) {
    float sw = static_cast<float>(screen_width());
    float bar_width = 400.0f;
    float slot_size = 44.0f;
    float slot_gap = 6.0f;
    float start_x = (sw - bar_width) / 2.0f;
    float y = static_cast<float>(screen_height()) - 60.0f;

    // Background bar
    ui.add_filled_rect(start_x - 5, y - 5, bar_width + 10, slot_size + 10, 0x80000000);

    constexpr int NUM_SKILL_SLOTS = 5;
    for (int i = 0; i < NUM_SKILL_SLOTS; ++i) {
        float x = start_x + i * (slot_size + slot_gap);
        const auto& slot = hud_state_.skill_slots[i];

        // Slot background
        uint32_t bg_color = slot.available ? 0xCC333333 : 0xCC111111;
        ui.add_filled_rect(x, y, slot_size, slot_size, bg_color);
        ui.add_rect_outline(x, y, slot_size, slot_size, 0xFF666666, 1.0f);

        // Keybind number
        char key[4];
        snprintf(key, sizeof(key), "%d", i + 1);
        ui.add_text(key, x + 2, y + 2, 0.6f, 0xFF888888);

        if (slot.available && !slot.name.empty()) {
            // Skill name (truncated)
            std::string label = slot.name.substr(0, 4);
            ui.add_text(label, x + 4, y + 16, 0.7f, ui_colors::WHITE);

            // Cooldown overlay
            if (slot.cooldown > 0.0f && slot.max_cooldown > 0.0f) {
                float ratio = slot.cooldown / slot.max_cooldown;
                float overlay_h = slot_size * ratio;
                ui.add_filled_rect(x, y + (slot_size - overlay_h), slot_size, overlay_h, 0x88000000);

                char cd[8];
                snprintf(cd, sizeof(cd), "%.1f", slot.cooldown);
                ui.add_text(cd, x + 8, y + 14, 0.8f, 0xFFFF4444);
            }
        }
    }
}

void Game::build_quest_tracker_ui(UIScene& ui) {
    if (hud_state_.tracked_quests.empty()) return;

    float x = static_cast<float>(screen_width()) - 260.0f;
    float y = 10.0f;
    float w = 250.0f;

    ui.add_filled_rect(x, y, w, 20.0f, 0x80000000);
    ui.add_text("QUESTS", x + 5, y + 3, 0.8f, 0xFFFFCC00);
    y += 22.0f;

    for (const auto& quest : hud_state_.tracked_quests) {
        ui.add_filled_rect(x, y, w, 16.0f, 0x60000000);
        ui.add_text(quest.quest_name, x + 5, y + 1, 0.7f, ui_colors::WHITE);
        y += 18.0f;

        for (const auto& obj : quest.objectives) {
            char progress[64];
            snprintf(progress, sizeof(progress), "  %s: %d/%d",
                     obj.description.c_str(), obj.current, obj.required);
            uint32_t color = obj.complete ? 0xFF00FF00 : 0xFFCCCCCC;
            ui.add_text(progress, x + 5, y + 1, 0.6f, color);
            y += 14.0f;
        }
        y += 4.0f;
    }
}

void Game::build_notifications_ui(UIScene& ui) {
    float cx = static_cast<float>(screen_width()) / 2.0f;
    float y = 80.0f;

    for (const auto& notif : hud_state_.notifications) {
        float alpha_ratio = std::min(notif.timer / 0.5f, 1.0f);  // Fade out last 0.5s
        uint8_t alpha = static_cast<uint8_t>(255 * alpha_ratio);
        uint32_t color = (notif.color & 0x00FFFFFF) | (alpha << 24);

        float text_w = static_cast<float>(notif.text.size()) * 8.0f;
        ui.add_filled_rect(cx - text_w / 2 - 10, y - 5, text_w + 20, 28.0f,
                           0x00000000 | (static_cast<uint32_t>(alpha * 0.6f) << 24));
        ui.add_text(notif.text, cx - text_w / 2, y, 1.2f, color);
        y += 35.0f;
    }
}

void Game::build_damage_numbers_ui(UIScene& ui) {
    auto cam_state = get_camera_state();

    for (const auto& dn : hud_state_.damage_numbers) {
        // Project world position to screen
        glm::vec4 clip = cam_state.view_projection * glm::vec4(dn.x, dn.y, dn.z, 1.0f);
        if (clip.w <= 0.0f) continue;  // Behind camera

        float ndc_x = clip.x / clip.w;
        float ndc_y = clip.y / clip.w;
        float sx = (ndc_x * 0.5f + 0.5f) * static_cast<float>(screen_width());
        float sy = (1.0f - (ndc_y * 0.5f + 0.5f)) * static_cast<float>(screen_height());

        float alpha_ratio = dn.alpha();
        uint8_t alpha = static_cast<uint8_t>(255 * alpha_ratio);

        uint32_t color;
        if (dn.is_heal) {
            color = 0x0000FF00 | (alpha << 24);  // Green for heals
        } else {
            color = 0x000000FF | (alpha << 24);  // Red for damage (ABGR)
        }

        char text[16];
        int val = static_cast<int>(dn.damage);
        if (dn.is_heal) {
            snprintf(text, sizeof(text), "+%d", val);
        } else {
            snprintf(text, sizeof(text), "%d", val);
        }

        float scale = 1.0f + (1.0f - alpha_ratio) * 0.3f;  // Grow slightly as fading
        ui.add_text(text, sx - 15.0f, sy, scale, color);
    }
}

void Game::build_dialogue_ui(UIScene& ui) {
    if (!hud_state_.dialogue.visible) return;

    float sw = static_cast<float>(screen_width());
    float sh = static_cast<float>(screen_height());
    float w = 500.0f;
    float h = 250.0f;
    float x = (sw - w) / 2.0f;
    float y = sh - h - 80.0f;

    // Panel background
    ui.add_filled_rect(x, y, w, h, 0xE0222222);
    ui.add_rect_outline(x, y, w, h, 0xFF888888, 2.0f);

    // NPC name header
    ui.add_filled_rect(x, y, w, 28.0f, 0xFF443322);
    ui.add_text(hud_state_.dialogue.npc_name, x + 10, y + 5, 1.0f, 0xFFFFCC00);

    // Dialogue text
    ui.add_text(hud_state_.dialogue.dialogue, x + 15, y + 40, 0.8f, ui_colors::WHITE);

    // Quest options
    float option_y = y + 100.0f;
    for (int i = 0; i < hud_state_.dialogue.quest_count; ++i) {
        bool selected = (i == hud_state_.dialogue.selected_option);
        uint32_t color = selected ? 0xFFFFFF00 : 0xFFCCCCCC;
        uint32_t bg = selected ? 0x40FFFFFF : 0x00000000;

        ui.add_filled_rect(x + 10, option_y, w - 20, 22.0f, bg);
        std::string option = std::string("[") + std::to_string(i + 1) + "] " +
                             hud_state_.dialogue.quest_names[i];
        ui.add_text(option, x + 15, option_y + 3, 0.8f, color);
        option_y += 26.0f;
    }

    // Close hint
    ui.add_text("[E] Close    [Enter] Accept", x + 10, y + h - 25, 0.7f, ui_colors::TEXT_HINT);
}

void Game::build_inventory_panel_ui(UIScene& ui) {
    float sw = static_cast<float>(screen_width());
    float sh = static_cast<float>(screen_height());
    float w = 350.0f;
    float h = 500.0f;
    float px = (sw - w) / 2.0f;
    float py = (sh - h) / 2.0f;

    ui.add_filled_rect(px, py, w, h, 0xE0222222);
    ui.add_rect_outline(px, py, w, h, 0xFF888888, 2.0f);

    // Title
    ui.add_filled_rect(px, py, w, 28.0f, 0xFF334455);
    ui.add_text("INVENTORY", px + 10, py + 5, 1.0f, ui_colors::WHITE);
    ui.add_text("[I] Close", px + w - 80, py + 8, 0.6f, ui_colors::TEXT_HINT);

    // Equipped items section
    float ey = py + 35.0f;
    ui.add_text("Equipped:", px + 10, ey, 0.8f, 0xFFCCCCCC);
    ey += 18.0f;

    char equip_text[64];
    snprintf(equip_text, sizeof(equip_text), "[1] Weapon: %s",
             panel_state_.equipped_weapon > 0 ? item_name(panel_state_.equipped_weapon) : "None");
    ui.add_text(equip_text, px + 15, ey, 0.7f,
                panel_state_.equipped_weapon > 0 ? 0xFF66AAFF : ui_colors::TEXT_DIM);
    ey += 16.0f;

    snprintf(equip_text, sizeof(equip_text), "[2] Armor: %s",
             panel_state_.equipped_armor > 0 ? item_name(panel_state_.equipped_armor) : "None");
    ui.add_text(equip_text, px + 15, ey, 0.7f,
                panel_state_.equipped_armor > 0 ? 0xFF66AAFF : ui_colors::TEXT_DIM);
    ey += 22.0f;

    // Divider
    ui.add_line(px + 10, ey, px + w - 10, ey, 0xFF444444, 1.0f);
    ey += 8.0f;

    ui.add_text("Backpack:  [Enter] Equip  [U] Use", px + 10, ey, 0.65f, ui_colors::TEXT_HINT);
    ey += 16.0f;

    // Inventory slots
    for (int i = 0; i < PanelState::MAX_INVENTORY_SLOTS; ++i) {
        bool selected = (i == panel_state_.inventory_cursor);
        float slot_y = ey + i * 20.0f;

        if (slot_y + 20.0f > py + h - 5.0f) break;  // Don't draw past panel

        if (selected) {
            ui.add_filled_rect(px + 5, slot_y, w - 10, 18.0f, 0x40FFFFFF);
        }

        const auto& slot = panel_state_.inventory_slots[i];
        if (slot.empty()) {
            ui.add_text("---", px + 15, slot_y + 2, 0.7f, 0xFF555555);
        } else {
            char item_text[64];
            snprintf(item_text, sizeof(item_text), "%s x%d",
                     item_name(slot.item_id), slot.count);
            ui.add_text(item_text, px + 15, slot_y + 2, 0.7f,
                        selected ? ui_colors::WHITE : 0xFFCCCCCC);
        }
    }
}

void Game::build_talent_panel_ui(UIScene& ui) {
    float sw = static_cast<float>(screen_width());
    float sh = static_cast<float>(screen_height());
    float w = 400.0f;
    float h = 450.0f;
    float px = (sw - w) / 2.0f;
    float py = (sh - h) / 2.0f;

    ui.add_filled_rect(px, py, w, h, 0xE0222222);
    ui.add_rect_outline(px, py, w, h, 0xFF888888, 2.0f);

    // Title
    ui.add_filled_rect(px, py, w, 28.0f, 0xFF553322);
    ui.add_text("TALENT TREE", px + 10, py + 5, 1.0f, ui_colors::WHITE);

    char pts[32];
    snprintf(pts, sizeof(pts), "Points: %d", panel_state_.talent_points);
    ui.add_text(pts, px + w - 100, py + 8, 0.7f, 0xFFFFCC00);

    ui.add_text("[T] Close  [Enter] Unlock", px + 10, py + h - 22, 0.6f, ui_colors::TEXT_HINT);

    // Talent list (simple vertical list for now)
    // Hardcoded talent names - would come from data in a full implementation
    static const char* talent_names[] = {
        "Strength I", "Vitality I", "Critical Strike",
        "Strength II", "Vitality II", "Evasion",
        "Power Strike", "Fortify", "Berserker"
    };
    static const int num_talents = 9;

    float ty = py + 40.0f;
    for (int i = 0; i < num_talents; ++i) {
        bool selected = (i == panel_state_.talent_cursor);
        uint16_t tid = static_cast<uint16_t>(i + 1);

        bool unlocked = false;
        for (uint16_t ut : panel_state_.unlocked_talents) {
            if (ut == tid) { unlocked = true; break; }
        }

        float slot_y = ty + i * 40.0f;
        if (slot_y + 35.0f > py + h - 30.0f) break;

        // Background
        uint32_t bg_color = selected ? 0x40FFFFFF : 0x20000000;
        ui.add_filled_rect(px + 10, slot_y, w - 20, 35.0f, bg_color);

        // Status indicator
        uint32_t status_color;
        const char* status_text;
        if (unlocked) {
            status_color = 0xFF00FF00;  // Green
            status_text = "[OK]";
        } else if (panel_state_.talent_points > 0) {
            status_color = 0xFF00AAFF;  // Orange/available
            status_text = "[  ]";
        } else {
            status_color = 0xFF666666;  // Locked
            status_text = "[--]";
        }

        ui.add_text(status_text, px + 15, slot_y + 3, 0.7f, status_color);
        ui.add_text(talent_names[i], px + 55, slot_y + 3, 0.8f,
                    unlocked ? 0xFF00FF00 : (selected ? ui_colors::WHITE : 0xFFCCCCCC));

        // Row/tier indicator
        char tier[16];
        snprintf(tier, sizeof(tier), "Tier %d", (i / 3) + 1);
        ui.add_text(tier, px + w - 80, slot_y + 5, 0.6f, 0xFF888888);
    }
}

void Game::build_quest_log_panel_ui(UIScene& ui) {
    float sw = static_cast<float>(screen_width());
    float sh = static_cast<float>(screen_height());
    float w = 400.0f;
    float h = 400.0f;
    float px = (sw - w) / 2.0f;
    float py = (sh - h) / 2.0f;

    ui.add_filled_rect(px, py, w, h, 0xE0222222);
    ui.add_rect_outline(px, py, w, h, 0xFF888888, 2.0f);

    // Title
    ui.add_filled_rect(px, py, w, 28.0f, 0xFF335533);
    ui.add_text("QUEST LOG", px + 10, py + 5, 1.0f, ui_colors::WHITE);
    ui.add_text("[L] Close  [X] Abandon", px + 10, py + h - 22, 0.6f, ui_colors::TEXT_HINT);

    if (hud_state_.tracked_quests.empty()) {
        ui.add_text("No active quests.", px + 20, py + 60, 0.9f, ui_colors::TEXT_DIM);
        return;
    }

    float qy = py + 40.0f;
    for (int i = 0; i < static_cast<int>(hud_state_.tracked_quests.size()); ++i) {
        const auto& quest = hud_state_.tracked_quests[i];
        bool selected = (i == panel_state_.quest_cursor);

        float entry_h = 20.0f + static_cast<float>(quest.objectives.size()) * 16.0f + 8.0f;
        if (qy + entry_h > py + h - 30.0f) break;

        if (selected) {
            ui.add_filled_rect(px + 5, qy, w - 10, entry_h, 0x40FFFFFF);
        }

        ui.add_text(quest.quest_name, px + 15, qy + 2, 0.9f,
                    selected ? 0xFFFFCC00 : ui_colors::WHITE);
        qy += 22.0f;

        for (const auto& obj : quest.objectives) {
            char progress[80];
            snprintf(progress, sizeof(progress), "  - %s: %d/%d",
                     obj.description.c_str(), obj.current, obj.required);
            uint32_t color = obj.complete ? 0xFF00FF00 : 0xFFCCCCCC;
            ui.add_text(progress, px + 20, qy, 0.7f, color);
            qy += 16.0f;
        }
        qy += 8.0f;
    }
}

} // namespace mmo::client
