#include "game.hpp"
#include "client/ecs/components.hpp"
#include "client/game_state.hpp"
#include "client/menu_system.hpp"
#include "engine/effect_types.hpp"
#include "engine/scene/camera_state.hpp"
#include "engine/scene/ui_scene.hpp"
#include "engine/systems/camera_controller.hpp"
#include "engine/model_loader.hpp"
#include "engine/render_constants.hpp"
#include "engine/heightmap.hpp"
#include "entt/entity/fwd.hpp"
#include "glm/ext/matrix_float4x4.hpp"
#include "glm/ext/matrix_transform.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "protocol/heightmap.hpp"
#include "protocol/protocol.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_set>
#include <random>
#include <cstring>
#include <cmath>
#include <vector>

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

    network_.set_message_callback(
        [this](MessageType type, const std::vector<uint8_t>& payload) {
            handle_network_message(type, payload);
        });

    game_state_ = GameState::Connecting;
    connecting_timer_ = 0.0f;

    menu_system_ = std::make_unique<MenuSystem>(input(), [this]() { quit(); });

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
    if (game_state_ == GameState::Playing || game_state_ == GameState::ClassSelect) {
        menu_system_->update(dt);
        apply_graphics_settings();
        apply_controls_settings();
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
    player_x_ = world_config_.world_width / 2.0f;
    player_z_ = world_config_.world_height / 2.0f;
    camera().set_yaw(0.0f);
    camera().set_pitch(30.0f);
    update_camera_smooth(0.016f);

    render_scene_.clear();
    render_scene_.set_draw_skybox(false);
    render_scene_.set_draw_mountains(false);
    render_scene_.set_draw_ground(false);
    render_scene_.set_draw_grass(false);
    ui_scene_.clear();

    build_class_select_ui(ui_scene_);

    menu_system_->build_ui(ui_scene_, static_cast<float>(screen_width()), static_cast<float>(screen_height()));

    render_frame(render_scene_, ui_scene_, get_camera_state(), 0.016f);
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
    player_x_ = world_config_.world_width / 2.0f;
    player_z_ = world_config_.world_height / 2.0f;
    camera().set_yaw(0.0f);
    camera().set_pitch(30.0f);
    update_camera_smooth(0.016f);

    render_scene_.clear();
    render_scene_.set_draw_skybox(false);
    render_scene_.set_draw_mountains(false);
    render_scene_.set_draw_ground(false);
    render_scene_.set_draw_grass(false);
    ui_scene_.clear();

    build_connecting_ui(ui_scene_);

    render_frame(render_scene_, ui_scene_, get_camera_state(), 0.016f);
}

void Game::update_playing(float dt) {
    if (!network_.is_connected()) {
        std::cout << "Lost connection to server" << std::endl;
        quit();
        return;
    }

    network_.poll_messages();

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
        net_input.attack_dir_x = eng_input.attack_dir_x;
        net_input.attack_dir_y = eng_input.attack_dir_y;
        network_.send_input(net_input);
        input().consume_attack();
    }
    input().reset_changed();

    local_player_id_ = network_.local_player_id();

    network_smoother_.update(registry_, dt);

    update_attack_effects(dt);

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

    // Set animations on entities before render
    auto view = registry_.view<ecs::NetworkId, ecs::Transform, ecs::Velocity, ecs::EntityInfo, ecs::Combat>();
    for (auto entity : view) {
        auto& info = registry_.get<ecs::EntityInfo>(entity);
        auto& vel = registry_.get<ecs::Velocity>(entity);
        auto& combat = registry_.get<ecs::Combat>(entity);

        if (info.model_name.empty()) continue;
        Model* model = models().get_model(info.model_name);
        if (!model || !model->has_skeleton) continue;

        std::string anim_name;
        if (combat.is_attacking) {
            anim_name = "Attack";
        } else if (std::abs(vel.x) > 1.0f || std::abs(vel.z) > 1.0f) {
            anim_name = "Walk";
        } else {
            anim_name = "Idle";
        }

        AnimationState* state = models().get_animation_state(info.model_name);
        if (state) {
            int clip_idx = model->find_animation(anim_name);
            if (clip_idx >= 0 && clip_idx != state->current_clip) {
                state->current_clip = clip_idx;
                state->time = 0.0f;
                state->playing = true;
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
    }

    camera().set_yaw(input().get_camera_yaw());
    camera().set_pitch(input().get_camera_pitch());

    bool sprinting = input().is_sprinting() &&
        (input().move_forward() || input().move_backward() ||
         input().move_left() || input().move_right());
    if (sprinting) {
        camera().set_mode(CameraMode::Sprint);
    }

    float zoom_delta = input().get_camera_zoom_delta();
    if (zoom_delta != 0.0f) {
        camera().adjust_zoom(zoom_delta);
    }
    input().reset_camera_deltas();

    update_camera_smooth(dt);

    glm::vec3 cam_forward = camera().get_forward();
    input().set_camera_forward(cam_forward.x, cam_forward.z);
}

void Game::render_playing() {
    render_scene_.clear();
    ui_scene_.clear();

    const auto& gfx = menu_system_->graphics_settings();
    render_scene_.set_draw_skybox(gfx.skybox_enabled);
    render_scene_.set_draw_mountains(gfx.mountains_enabled);
    render_scene_.set_draw_rocks(gfx.rocks_enabled);
    render_scene_.set_draw_trees(gfx.trees_enabled);
    render_scene_.set_draw_ground(true);
    render_scene_.set_draw_grass(gfx.grass_enabled);

    // Add effects to scene
    for (const auto& effect : attack_effects_) {
        engine::EffectInstance ei;
        ei.effect_type = effect.effect_type;
        ei.model_name = effect.effect_model;
        ei.x = effect.x;
        ei.y = effect.y;
        ei.direction_x = effect.direction_x;
        ei.direction_y = effect.direction_y;
        ei.timer = effect.timer;
        ei.duration = effect.duration;
        ei.range = effect.range;
        render_scene_.add_effect(ei);
    }

    // Add entities to scene as model commands
    // Distance cull from player position (camera is 200-350 units behind player)
    float draw_dist = gfx.get_draw_distance();
    float ENTITY_DRAW_DISTANCE_SQ = draw_dist * draw_dist;

    auto entity_view = registry_.view<ecs::NetworkId, ecs::Transform, ecs::Health, ecs::EntityInfo, ecs::Name>();
    for (auto entity : entity_view) {
        auto& net_id = registry_.get<ecs::NetworkId>(entity);
        bool is_local = (net_id.id == local_player_id_);

        auto& info = registry_.get<ecs::EntityInfo>(entity);
        // Filter trees/rocks by scene flags
        if (info.type == EntityType::Environment) {
            bool is_tree = (info.model_name.compare(0, 5, "tree_") == 0);
            if (is_tree && !render_scene_.should_draw_trees()) continue;
            if (!is_tree && !render_scene_.should_draw_rocks()) continue;
        }

        // Distance culling from player position (always render local player)
        if (!is_local) {
            auto& transform = registry_.get<ecs::Transform>(entity);
            float dx = transform.x - player_x_;
            float dz = transform.z - player_z_;
            if (dx * dx + dz * dz > ENTITY_DRAW_DISTANCE_SQ) continue;
        }

        add_entity_to_scene(entity, is_local);
    }

    build_playing_ui(ui_scene_);

    menu_system_->build_ui(ui_scene_, static_cast<float>(screen_width()), static_cast<float>(screen_height()));

    render_frame(render_scene_, ui_scene_, get_camera_state(), 0.016f);
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

    // Compute rotation
    float rotation = 0.0f;
    if (info.type == EntityType::Building || info.type == EntityType::Environment) {
        rotation = transform.rotation;
    } else if (info.type == EntityType::Player) {
        if (auto* attack_dir = registry_.try_get<ecs::AttackDirection>(entity)) {
            rotation = std::atan2(attack_dir->x, attack_dir->y);
        }
    } else {
        if (auto* vel = registry_.try_get<ecs::Velocity>(entity)) {
            if (vel->x != 0.0f || vel->z != 0.0f) {
                rotation = std::atan2(vel->x, vel->z);
            }
        }
    }

    // Compute scale
    float target_size = info.target_size;
    float model_size = model->max_dimension();
    float scale = (target_size * 1.5f) / model_size;

    // Compute attack tilt
    float attack_tilt = 0.0f;
    if (auto* combat = registry_.try_get<ecs::Combat>(entity)) {
        if (combat->is_attacking && combat->current_cooldown > 0.0f) {
            float max_cooldown = 0.5f;
            float progress = std::min(combat->current_cooldown / max_cooldown, 1.0f);
            attack_tilt = std::sin(progress * 3.14159f) * 0.4f;
        }
    }

    // Build transform matrix
    glm::vec3 position(transform.x, transform.y, transform.z);
    glm::mat4 model_mat = glm::mat4(1.0f);
    model_mat = glm::translate(model_mat, position);
    model_mat = glm::rotate(model_mat, rotation, glm::vec3(0.0f, 1.0f, 0.0f));
    if (attack_tilt != 0.0f) {
        model_mat = glm::rotate(model_mat, attack_tilt, glm::vec3(1.0f, 0.0f, 0.0f));
    }
    model_mat = glm::scale(model_mat, glm::vec3(scale));

    // Center the model on its base
    float cx = (model->min_x + model->max_x) / 2.0f;
    float cy = model->min_y;
    float cz = (model->min_z + model->max_z) / 2.0f;
    model_mat = model_mat * glm::translate(glm::mat4(1.0f), glm::vec3(-cx, -cy, -cz));

    glm::vec4 tint(1.0f);

    // Add model command to scene
    if (model->has_skeleton) {
        AnimationState* anim_state = models().get_animation_state(info.model_name);
        std::array<glm::mat4, 64> bones;
        if (anim_state) {
            bones = anim_state->bone_matrices;
        } else {
            bones.fill(glm::mat4(1.0f));
        }
        render_scene_.add_skinned_model(info.model_name, model_mat, bones, tint);
    } else {
        render_scene_.add_model(info.model_name, model_mat, tint, attack_tilt);
    }

    // Add enemy health bar via UI scene
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
                world_config_.deserialize(payload.data());
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
        case MessageType::WorldState:
            on_world_state(payload);
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
        default:
            break;
    }
}

void Game::on_connection_accepted(const std::vector<uint8_t>& payload) {
    if (payload.size() >= 4) {
        uint32_t id;
        std::memcpy(&id, payload.data(), sizeof(id));
        if (id == 0) {
            std::cout << "Connection accepted, waiting for class list..." << std::endl;
        } else {
            local_player_id_ = id;
            std::cout << "Spawned with player ID: " << local_player_id_ << std::endl;
            if (game_state_ == GameState::Spawning) {
                game_state_ = GameState::Playing;
            }
        }
    }
}

void Game::on_class_list(const std::vector<uint8_t>& payload) {
    if (payload.empty()) return;

    uint8_t count = payload[0];
    available_classes_.clear();
    available_classes_.resize(count);

    size_t offset = 1;
    for (uint8_t i = 0; i < count && offset + ClassInfo::serialized_size() <= payload.size(); ++i) {
        available_classes_[i].deserialize(payload.data() + offset);
        offset += ClassInfo::serialized_size();
    }

    std::cout << "Received " << static_cast<int>(count) << " classes from server" << std::endl;

    if (game_state_ == GameState::Connecting) {
        game_state_ = GameState::ClassSelect;
        selected_class_index_ = 0;
    }
}

void Game::on_heightmap_chunk(const std::vector<uint8_t>& payload) {
    heightmap_ = std::make_unique<HeightmapChunk>();
    if (heightmap_->deserialize(payload.data(), payload.size())) {
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

void Game::on_world_state(const std::vector<uint8_t>& payload) {
    if (payload.size() < 2) return;

    uint16_t entity_count;
    std::memcpy(&entity_count, payload.data(), sizeof(entity_count));

    size_t offset = sizeof(entity_count);

    std::unordered_set<uint32_t> received_ids;

    for (uint16_t i = 0; i < entity_count && offset + EntityState::serialized_size() <= payload.size(); ++i) {
        NetEntityState state;
        state.deserialize(payload.data() + offset);
        offset += EntityState::serialized_size();

        received_ids.insert(state.id);

        bool was_attacking = prev_attacking_[state.id];
        if (state.is_attacking && !was_attacking) {
            float dir_x = state.attack_dir_x;
            float dir_y = state.attack_dir_y;

            float len = std::sqrt(dir_x * dir_x + dir_y * dir_y);
            if (len < 0.001f) {
                dir_x = 0;
                dir_y = 1;
            } else {
                dir_x /= len;
                dir_y /= len;
            }

            spawn_attack_effect(state, dir_x, dir_y);

            if (state.id == local_player_id_) {
                camera().notify_attack();
            }
        }
        prev_attacking_[state.id] = state.is_attacking;

        entt::entity entity = find_or_create_entity(state.id);
        update_entity_from_state(entity, state);
    }

    std::vector<uint32_t> to_remove;
    for (const auto& [net_id, entity] : network_to_entity_) {
        if (received_ids.find(net_id) == received_ids.end()) {
            to_remove.push_back(net_id);
        }
    }
    for (uint32_t id : to_remove) {
        remove_entity(id);
        prev_attacking_.erase(id);
    }
}

void Game::on_player_joined(const std::vector<uint8_t>& payload) {
    if (payload.size() >= EntityState::serialized_size()) {
        NetEntityState state;
        state.deserialize(payload.data());

        entt::entity entity = find_or_create_entity(state.id);
        update_entity_from_state(entity, state);

        std::cout << "Player joined: " << state.name << " (ID: " << state.id << ")" << std::endl;
    }
}

void Game::on_player_left(const std::vector<uint8_t>& payload) {
    if (payload.size() >= 4) {
        uint32_t player_id;
        std::memcpy(&player_id, payload.data(), sizeof(player_id));

        auto it = network_to_entity_.find(player_id);
        if (it != network_to_entity_.end()) {
            if (registry_.valid(it->second)) {
                auto* name = registry_.try_get<ecs::Name>(it->second);
                if (name) {
                    std::cout << "Player left: " << name->value << " (ID: " << player_id << ")" << std::endl;
                }
            }
            remove_entity(player_id);
            prev_attacking_.erase(player_id);
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
    info.effect_model = state.effect_model;
    info.effect_duration = state.effect_duration;
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
    ecs::AttackEffect effect;
    effect.effect_type = state.effect_type;
    effect.effect_model = state.effect_model;
    effect.x = state.x;
    effect.y = state.z;
    effect.direction_x = dir_x;
    effect.direction_y = dir_y;
    effect.duration = state.effect_duration;
    effect.range = state.cone_angle;
    effect.cone_angle = state.cone_angle;

    if (effect.effect_type == "orbit") {
        effect.target_x = state.x + dir_x * state.cone_angle * 0.5f;
        effect.target_y = state.z + dir_y * state.cone_angle * 0.5f;
    }

    effect.timer = effect.duration;
    if (effect.duration > 0.0f) {
        attack_effects_.push_back(effect);
    }
}

void Game::update_attack_effects(float dt) {
    for (auto it = attack_effects_.begin(); it != attack_effects_.end(); ) {
        it->timer -= dt;
        if (it->timer <= 0.0f) {
            it = attack_effects_.erase(it);
        } else {
            ++it;
        }
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
    camera().set_terrain_height_func([this](float x, float z) {
        return get_terrain_height(x, z);
    });

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
    set_fullscreen(gfx.window_mode == 1);
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
    ui.add_text("SELECT YOUR CLASS", center_x - 150.0f, 30.0f, 2.0f, ui_colors::WHITE);
    ui.add_text("Use A/D to select, SPACE to confirm", center_x - 160.0f, 70.0f, 1.0f, ui_colors::TEXT_DIM);

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
        float name_x = x - 40.0f;
        ui.add_text(cls.name, name_x, box_y + box_size/2 + 15.0f, 1.0f, text_color);
        ui.add_text(cls.short_desc, x - 55.0f, box_y + box_size/2 + 40.0f, 0.8f, ui_colors::TEXT_DIM);
    }

    const auto& sel = available_classes_[selected_class_index_];
    ui.add_filled_rect(center_x - 200, screen_height() - 120, 400, 80, ui_colors::PANEL_BG);
    ui.add_rect_outline(center_x - 200, screen_height() - 120, 400, 80, sel.select_color, 2.0f);

    ui.add_text(sel.desc_line1, center_x - 180.0f, screen_height() - 105.0f, 0.9f, ui_colors::WHITE);
    ui.add_text(sel.desc_line2, center_x - 180.0f, screen_height() - 80.0f, 0.9f, ui_colors::WHITE);

    ui.add_text("Press ESC anytime to open Settings Menu", center_x - 150.0f, screen_height() - 25.0f, 0.8f, ui_colors::TEXT_HINT);
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
}

// ============================================================================
// Delta Compression Handlers
// ============================================================================

void Game::on_entity_enter(const std::vector<uint8_t>& payload) {
    // Deserialize full entity state
    NetEntityState state;
    state.deserialize(payload.data());

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
    delta.deserialize(payload.data(), payload.size());

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
    if (payload.size() >= 4) {
        uint32_t entity_id = 0;
        std::memcpy(&entity_id, payload.data(), sizeof(entity_id));

        remove_entity(entity_id);
        prev_attacking_.erase(entity_id);
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
                    std::strncpy(state.effect_model, info.effect_model.c_str(), 31);
                    state.effect_model[31] = '\0';
                    state.effect_duration = info.effect_duration;
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

} // namespace mmo::client
