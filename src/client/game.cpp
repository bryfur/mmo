#include "game.hpp"
#include "engine/render_constants.hpp"
#include "engine/heightmap.hpp"
#include "common/heightmap.hpp"
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <unordered_set>
#include <random>
#include <cstring>
#include <cmath>

namespace mmo {

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
    if (!context_.init(1280, 720, "MMO Client - Select Class")) {
        return false;
    }

    if (!scene_renderer_.init(context_)) {
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

    init_menu_items();

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
    scene_renderer_.shutdown();
    context_.shutdown();
}

void Game::on_update(float dt) {
    if (game_state_ == GameState::Playing || game_state_ == GameState::ClassSelect) {
        update_menu(dt);
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
    camera_system_.set_yaw(0.0f);
    camera_system_.set_pitch(30.0f);
    update_camera_smooth(0.016f);

    render_scene_.clear();
    ui_scene_.clear();

    build_class_select_ui(ui_scene_);

    if (menu_open_) {
        build_menu_ui(ui_scene_);
    }

    scene_renderer_.render_frame(render_scene_, ui_scene_, get_camera_state(), 0.016f);
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
    camera_system_.set_yaw(0.0f);
    camera_system_.set_pitch(30.0f);
    update_camera_smooth(0.016f);

    render_scene_.clear();
    ui_scene_.clear();

    build_connecting_ui(ui_scene_);

    scene_renderer_.render_frame(render_scene_, ui_scene_, get_camera_state(), 0.016f);
}

void Game::update_playing(float dt) {
    if (!network_.is_connected()) {
        std::cout << "Lost connection to server" << std::endl;
        quit();
        return;
    }

    network_.poll_messages();

    input().set_player_screen_pos(context_.width() / 2.0f, context_.height() / 2.0f);

    static float input_send_timer = 0.0f;
    input_send_timer += dt;
    if (input_send_timer >= 0.016f) {
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
        input_send_timer = 0.0f;
    }
    input().reset_changed();

    local_player_id_ = network_.local_player_id();

    network_smoother_.update(registry_, dt);

    update_attack_effects(dt);

    // Set animations on entities before render
    auto view = registry_.view<ecs::NetworkId, ecs::Transform, ecs::Velocity, ecs::EntityInfo, ecs::Combat>();
    for (auto entity : view) {
        auto& info = registry_.get<ecs::EntityInfo>(entity);
        auto& vel = registry_.get<ecs::Velocity>(entity);
        auto& combat = registry_.get<ecs::Combat>(entity);

        if (info.model_name.empty()) continue;
        Model* model = scene_renderer_.models().get_model(info.model_name);
        if (!model || !model->has_skeleton) continue;

        std::string anim_name;
        if (combat.is_attacking) {
            anim_name = "Attack";
        } else if (std::abs(vel.x) > 1.0f || std::abs(vel.y) > 1.0f) {
            anim_name = "Walk";
        } else {
            anim_name = "Idle";
        }

        AnimationState* state = scene_renderer_.models().get_animation_state(info.model_name);
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
        player_z_ = transform.y;

        if (auto* vel = registry_.try_get<ecs::Velocity>(it->second)) {
            camera_system_.set_target_velocity(glm::vec3(vel->x, 0.0f, vel->y));
        }

        if (auto* combat = registry_.try_get<ecs::Combat>(it->second)) {
            camera_system_.set_in_combat(combat->is_attacking || combat->current_cooldown > 0);
        }
    }

    camera_system_.set_yaw(input().get_camera_yaw());
    camera_system_.set_pitch(input().get_camera_pitch());

    bool sprinting = input().is_sprinting() &&
        (input().move_forward() || input().move_backward() ||
         input().move_left() || input().move_right());
    if (sprinting) {
        camera_system_.set_mode(CameraMode::Sprint);
    }

    float zoom_delta = input().get_camera_zoom_delta();
    if (zoom_delta != 0.0f) {
        camera_system_.adjust_zoom(zoom_delta);
    }
    input().reset_camera_deltas();

    update_camera_smooth(dt);

    glm::vec3 cam_forward = camera_system_.get_forward();
    input().set_camera_forward(cam_forward.x, cam_forward.z);
}

void Game::render_playing() {
    render_scene_.clear();
    ui_scene_.clear();

    render_scene_.set_draw_skybox(graphics_settings_.skybox_enabled);
    render_scene_.set_draw_mountains(graphics_settings_.mountains_enabled);
    render_scene_.set_draw_rocks(graphics_settings_.rocks_enabled);
    render_scene_.set_draw_trees(graphics_settings_.trees_enabled);
    render_scene_.set_draw_ground(true);
    render_scene_.set_draw_grass(graphics_settings_.grass_enabled);

    // Add effects to scene
    for (const auto& effect : attack_effects_) {
        engine::EffectInstance ei;
        ei.effect_type = effect.effect_type;
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

        add_entity_to_scene(entity, is_local);
    }

    build_playing_ui(ui_scene_);

    if (menu_open_) {
        build_menu_ui(ui_scene_);
    }

    scene_renderer_.render_frame(render_scene_, ui_scene_, get_camera_state(), 0.016f);
}

// ============================================================================
// Entity → Scene Command
// ============================================================================

void Game::add_entity_to_scene(entt::entity entity, bool is_local) {
    auto& transform = registry_.get<ecs::Transform>(entity);
    auto& health = registry_.get<ecs::Health>(entity);
    auto& info = registry_.get<ecs::EntityInfo>(entity);

    if (info.model_name.empty()) return;

    Model* model = scene_renderer_.models().get_model(info.model_name);
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
            if (vel->x != 0.0f || vel->y != 0.0f) {
                rotation = std::atan2(vel->x, vel->y);
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
    glm::vec3 position(transform.x, transform.z, transform.y);
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
        AnimationState* anim_state = scene_renderer_.models().get_animation_state(info.model_name);
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
        float bar_height_offset = transform.z + target_size * 1.3f;
        ui_scene_.add_enemy_health_bar_3d(transform.x, bar_height_offset, transform.y,
                                           target_size * 0.8f, health_ratio);
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

        scene_renderer_.set_heightmap(engine_hm);
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
                camera_system_.notify_attack();
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
    velocity.y = state.vy;

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
    effect.y = state.y;
    effect.direction_x = dir_x;
    effect.direction_y = dir_y;
    effect.duration = state.effect_duration;
    effect.range = state.cone_angle;
    effect.cone_angle = state.cone_angle;

    if (effect.effect_type == "orbit") {
        effect.target_x = state.x + dir_x * state.cone_angle * 0.5f;
        effect.target_y = state.y + dir_y * state.cone_angle * 0.5f;
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
    auto& models = scene_renderer_.models();
    std::string models_path = assets_path + "/models/";

    bool success = true;

    if (!models.load_model("warrior", models_path + "warrior_rigged.glb")) {
        success &= models.load_model("warrior", models_path + "warrior.glb");
    }
    if (!models.load_model("mage", models_path + "mage_rigged.glb")) {
        success &= models.load_model("mage", models_path + "mage.glb");
    }
    if (!models.load_model("paladin", models_path + "paladin_rigged.glb")) {
        success &= models.load_model("paladin", models_path + "paladin.glb");
    }
    if (!models.load_model("archer", models_path + "archer_rigged.glb")) {
        success &= models.load_model("archer", models_path + "archer.glb");
    }
    success &= models.load_model("npc", models_path + "npc_enemy.glb");

    models.load_model("ground_grass", models_path + "ground_grass.glb");
    models.load_model("ground_stone", models_path + "ground_stone.glb");

    models.load_model("mountain_small", models_path + "mountain_small.glb");
    models.load_model("mountain_medium", models_path + "mountain_medium.glb");
    models.load_model("mountain_large", models_path + "mountain_large.glb");

    models.load_model("building_tavern", models_path + "building_tavern.glb");
    models.load_model("building_blacksmith", models_path + "building_blacksmith.glb");
    models.load_model("building_tower", models_path + "building_tower.glb");
    models.load_model("building_shop", models_path + "building_shop.glb");
    models.load_model("building_well", models_path + "building_well.glb");
    models.load_model("building_house", models_path + "building_house.glb");
    models.load_model("building_inn", models_path + "inn.glb");
    models.load_model("wooden_log", models_path + "wooden_log.glb");
    models.load_model("log_tower", models_path + "log_tower.glb");

    models.load_model("npc_merchant", models_path + "npc_merchant.glb");
    models.load_model("npc_guard", models_path + "npc_guard.glb");
    models.load_model("npc_blacksmith", models_path + "npc_blacksmith.glb");
    models.load_model("npc_innkeeper", models_path + "npc_innkeeper.glb");
    models.load_model("npc_villager", models_path + "npc_villager.glb");

    models.load_model("weapon_sword", models_path + "weapon_sword.glb");
    models.load_model("spell_fireball", models_path + "spell_fireball.glb");
    models.load_model("spell_bible", models_path + "spell_bible.glb");

    models.load_model("rock_boulder", models_path + "rock_boulder.glb");
    models.load_model("rock_slate", models_path + "rock_slate.glb");
    models.load_model("rock_spire", models_path + "rock_spire.glb");
    models.load_model("rock_cluster", models_path + "rock_cluster.glb");
    models.load_model("rock_mossy", models_path + "rock_mossy.glb");

    models.load_model("tree_oak", models_path + "tree_oak.glb");
    models.load_model("tree_pine", models_path + "tree_pine.glb");
    models.load_model("tree_dead", models_path + "tree_dead.glb");

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
    camera_system_.set_screen_size(context_.width(), context_.height());
    camera_system_.set_terrain_height_func([this](float x, float z) {
        return scene_renderer_.get_terrain_height(x, z);
    });

    float terrain_y = scene_renderer_.get_terrain_height(player_x_, player_z_);
    camera_system_.set_target(glm::vec3(player_x_, terrain_y, player_z_));
    camera_system_.update(dt);
}

engine::CameraState Game::get_camera_state() const {
    engine::CameraState state;
    state.view = camera_system_.get_view_matrix();
    state.projection = camera_system_.get_projection_matrix();
    state.position = camera_system_.get_position();
    return state;
}

// ============================================================================
// MENU SYSTEM
// ============================================================================

void Game::init_menu_items() {
    current_menu_page_ = MenuPage::Main;
    init_main_menu();
}

void Game::init_main_menu() {
    menu_items_.clear();
    menu_selected_index_ = 0;

    MenuItem controls_item;
    controls_item.label = "Controls";
    controls_item.type = MenuItemType::Submenu;
    controls_item.target_page = MenuPage::Controls;
    menu_items_.push_back(controls_item);

    MenuItem graphics_item;
    graphics_item.label = "Graphics";
    graphics_item.type = MenuItemType::Submenu;
    graphics_item.target_page = MenuPage::Graphics;
    menu_items_.push_back(graphics_item);

    MenuItem resume_item;
    resume_item.label = "Resume Game";
    resume_item.type = MenuItemType::Button;
    resume_item.action = [this]() {
        menu_open_ = false;
        input().set_game_input_enabled(true);
    };
    menu_items_.push_back(resume_item);

    MenuItem quit_item;
    quit_item.label = "Quit to Desktop";
    quit_item.type = MenuItemType::Button;
    quit_item.action = [this]() {
        quit();
    };
    menu_items_.push_back(quit_item);
}

void Game::init_controls_menu() {
    menu_items_.clear();
    menu_selected_index_ = 0;

    MenuItem mouse_sens;
    mouse_sens.label = "Mouse Sensitivity";
    mouse_sens.type = MenuItemType::FloatSlider;
    mouse_sens.float_value = &controls_settings_.mouse_sensitivity;
    mouse_sens.float_min = 0.05f;
    mouse_sens.float_max = 1.0f;
    mouse_sens.float_step = 0.05f;
    menu_items_.push_back(mouse_sens);

    MenuItem ctrl_sens;
    ctrl_sens.label = "Controller Sensitivity";
    ctrl_sens.type = MenuItemType::FloatSlider;
    ctrl_sens.float_value = &controls_settings_.controller_sensitivity;
    ctrl_sens.float_min = 0.5f;
    ctrl_sens.float_max = 5.0f;
    ctrl_sens.float_step = 0.25f;
    menu_items_.push_back(ctrl_sens);

    MenuItem invert_x;
    invert_x.label = "Invert Camera X";
    invert_x.type = MenuItemType::Toggle;
    invert_x.toggle_value = &controls_settings_.invert_camera_x;
    menu_items_.push_back(invert_x);

    MenuItem invert_y;
    invert_y.label = "Invert Camera Y";
    invert_y.type = MenuItemType::Toggle;
    invert_y.toggle_value = &controls_settings_.invert_camera_y;
    menu_items_.push_back(invert_y);

    MenuItem back_item;
    back_item.label = "< Back";
    back_item.type = MenuItemType::Submenu;
    back_item.target_page = MenuPage::Main;
    menu_items_.push_back(back_item);
}

void Game::init_graphics_menu() {
    menu_items_.clear();
    menu_selected_index_ = 0;

    MenuItem fog;
    fog.label = "Fog";
    fog.type = MenuItemType::Toggle;
    fog.toggle_value = &graphics_settings_.fog_enabled;
    menu_items_.push_back(fog);

    MenuItem grass;
    grass.label = "Grass";
    grass.type = MenuItemType::Toggle;
    grass.toggle_value = &graphics_settings_.grass_enabled;
    menu_items_.push_back(grass);

    MenuItem skybox;
    skybox.label = "Skybox";
    skybox.type = MenuItemType::Toggle;
    skybox.toggle_value = &graphics_settings_.skybox_enabled;
    menu_items_.push_back(skybox);

    MenuItem mountains;
    mountains.label = "Mountains";
    mountains.type = MenuItemType::Toggle;
    mountains.toggle_value = &graphics_settings_.mountains_enabled;
    menu_items_.push_back(mountains);

    MenuItem trees;
    trees.label = "Trees";
    trees.type = MenuItemType::Toggle;
    trees.toggle_value = &graphics_settings_.trees_enabled;
    menu_items_.push_back(trees);

    MenuItem rocks;
    rocks.label = "Rocks";
    rocks.type = MenuItemType::Toggle;
    rocks.toggle_value = &graphics_settings_.rocks_enabled;
    menu_items_.push_back(rocks);

    MenuItem aniso;
    aniso.label = "Anisotropic Filter";
    aniso.type = MenuItemType::Slider;
    aniso.slider_value = &graphics_settings_.anisotropic_filter;
    aniso.slider_min = 0;
    aniso.slider_max = 4;
    aniso.slider_labels = {"Off", "2x", "4x", "8x", "16x"};
    menu_items_.push_back(aniso);

    MenuItem vsync;
    vsync.label = "VSync";
    vsync.type = MenuItemType::Slider;
    vsync.slider_value = &graphics_settings_.vsync_mode;
    vsync.slider_min = 0;
    vsync.slider_max = 2;
    vsync.slider_labels = {"Off", "Double Buffer", "Triple Buffer"};
    menu_items_.push_back(vsync);

    MenuItem fps_counter;
    fps_counter.label = "Show FPS";
    fps_counter.type = MenuItemType::Toggle;
    fps_counter.toggle_value = &graphics_settings_.show_fps;
    menu_items_.push_back(fps_counter);

    MenuItem back_item;
    back_item.label = "< Back";
    back_item.type = MenuItemType::Submenu;
    back_item.target_page = MenuPage::Main;
    menu_items_.push_back(back_item);
}

void Game::update_menu(float dt) {
    menu_highlight_progress_ = std::min(1.0f, menu_highlight_progress_ + dt * 8.0f);

    if (menu_selected_index_ != prev_menu_selected_) {
        menu_highlight_progress_ = 0.0f;
        prev_menu_selected_ = menu_selected_index_;
    }

    if (input().menu_toggle_pressed()) {
        if (current_menu_page_ != MenuPage::Main) {
            current_menu_page_ = MenuPage::Main;
            init_main_menu();
        } else {
            menu_open_ = !menu_open_;
            input().set_game_input_enabled(!menu_open_);
        }
        input().clear_menu_inputs();
        return;
    }

    if (!menu_open_) return;

    if (input().menu_up_pressed()) {
        menu_selected_index_ = (menu_selected_index_ - 1 + static_cast<int>(menu_items_.size())) % static_cast<int>(menu_items_.size());
    }
    if (input().menu_down_pressed()) {
        menu_selected_index_ = (menu_selected_index_ + 1) % static_cast<int>(menu_items_.size());
    }

    MenuItem& item = menu_items_[menu_selected_index_];
    if (item.type == MenuItemType::Toggle) {
        if (input().menu_select_pressed() || input().menu_left_pressed() || input().menu_right_pressed()) {
            if (item.toggle_value) {
                *item.toggle_value = !*item.toggle_value;
                apply_graphics_settings();
                apply_controls_settings();
            }
        }
    } else if (item.type == MenuItemType::Slider) {
        if (item.slider_value) {
            if (input().menu_left_pressed()) {
                *item.slider_value = std::max(item.slider_min, *item.slider_value - 1);
                apply_graphics_settings();
            }
            if (input().menu_right_pressed()) {
                *item.slider_value = std::min(item.slider_max, *item.slider_value + 1);
                apply_graphics_settings();
            }
        }
    } else if (item.type == MenuItemType::FloatSlider) {
        if (item.float_value) {
            if (input().menu_left_pressed()) {
                *item.float_value = std::max(item.float_min, *item.float_value - item.float_step);
                apply_controls_settings();
            }
            if (input().menu_right_pressed()) {
                *item.float_value = std::min(item.float_max, *item.float_value + item.float_step);
                apply_controls_settings();
            }
        }
    } else if (item.type == MenuItemType::Button) {
        if (input().menu_select_pressed()) {
            if (item.action) {
                item.action();
            }
        }
    } else if (item.type == MenuItemType::Submenu) {
        if (input().menu_select_pressed()) {
            current_menu_page_ = item.target_page;
            switch (item.target_page) {
                case MenuPage::Main:
                    init_main_menu();
                    break;
                case MenuPage::Controls:
                    init_controls_menu();
                    break;
                case MenuPage::Graphics:
                    init_graphics_menu();
                    break;
            }
        }
    }

    input().clear_menu_inputs();
}

void Game::apply_graphics_settings() {
    scene_renderer_.set_graphics_settings(graphics_settings_);
    scene_renderer_.set_anisotropic_filter(graphics_settings_.anisotropic_filter);
    scene_renderer_.set_vsync_mode(graphics_settings_.vsync_mode);
}

void Game::apply_controls_settings() {
    input().set_mouse_sensitivity(controls_settings_.mouse_sensitivity);
    input().set_controller_sensitivity(controls_settings_.controller_sensitivity);
    input().set_camera_x_inverted(controls_settings_.invert_camera_x);
    input().set_camera_y_inverted(controls_settings_.invert_camera_y);
}

// ============================================================================
// Scene Building Helpers
// ============================================================================

void Game::build_class_select_ui(UIScene& ui) {
    float center_x = context_.width() / 2.0f;
    float center_y = context_.height() / 2.0f;
    int num_classes = static_cast<int>(available_classes_.size());
    if (num_classes == 0) return;

    ui.add_filled_rect(0, 0, static_cast<float>(context_.width()), 100, ui_colors::TITLE_BG);
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
    ui.add_filled_rect(center_x - 200, context_.height() - 120, 400, 80, ui_colors::PANEL_BG);
    ui.add_rect_outline(center_x - 200, context_.height() - 120, 400, 80, sel.select_color, 2.0f);

    ui.add_text(sel.desc_line1, center_x - 180.0f, context_.height() - 105.0f, 0.9f, ui_colors::WHITE);
    ui.add_text(sel.desc_line2, center_x - 180.0f, context_.height() - 80.0f, 0.9f, ui_colors::WHITE);

    ui.add_text("Press ESC anytime to open Settings Menu", center_x - 150.0f, context_.height() - 25.0f, 0.8f, ui_colors::TEXT_HINT);
}

void Game::build_connecting_ui(UIScene& ui) {
    float center_x = context_.width() / 2.0f;
    float center_y = context_.height() / 2.0f;

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
        ui.add_target_reticle();
    }

    auto it = network_to_entity_.find(local_player_id_);
    if (it != network_to_entity_.end() && registry_.valid(it->second)) {
        auto& health = registry_.get<ecs::Health>(it->second);
        float health_ratio = health.current / health.max;
        ui.add_player_health_bar(health_ratio, health.max);
    }

    if (graphics_settings_.show_fps) {
        char fps_text[32];
        snprintf(fps_text, sizeof(fps_text), "FPS: %.0f", fps());
        ui.add_text(fps_text, 10.0f, 10.0f, 1.0f, ui_colors::FPS_TEXT);
    }
}

void Game::build_menu_ui(UIScene& ui) {
    if (!menu_open_) return;

    float screen_w = static_cast<float>(context_.width());
    float screen_h = static_cast<float>(context_.height());

    float panel_w = 550.0f;
    float panel_h = 70.0f + menu_items_.size() * 50.0f + 50.0f;
    float panel_x = (screen_w - panel_w) / 2.0f;
    float panel_y = (screen_h - panel_h) / 2.0f;

    ui.add_filled_rect(panel_x, panel_y, panel_w, panel_h, ui_colors::MENU_BG);
    ui.add_rect_outline(panel_x, panel_y, panel_w, panel_h, ui_colors::WHITE, 2.0f);

    const char* title = "SETTINGS";
    switch (current_menu_page_) {
        case MenuPage::Main: title = "SETTINGS"; break;
        case MenuPage::Controls: title = "CONTROLS"; break;
        case MenuPage::Graphics: title = "GRAPHICS"; break;
    }
    ui.add_text(title, panel_x + panel_w / 2.0f - 60.0f, panel_y + 15.0f, 1.5f, ui_colors::WHITE);

    float item_y = panel_y + 70.0f;
    for (size_t i = 0; i < menu_items_.size(); ++i) {
        const MenuItem& item = menu_items_[i];
        bool selected = (static_cast<int>(i) == menu_selected_index_);

        if (selected) {
            ui.add_filled_rect(panel_x + 10.0f, item_y, panel_w - 20.0f, 40.0f, ui_colors::SELECTION);
        }

        uint32_t text_color = selected ? ui_colors::WHITE : ui_colors::TEXT_DIM;
        ui.add_text(item.label, panel_x + 30.0f, item_y + 10.0f, 1.0f, text_color);

        if (item.type == MenuItemType::Toggle && item.toggle_value) {
            std::string value_str = *item.toggle_value ? "ON" : "OFF";
            uint32_t value_color = *item.toggle_value ? ui_colors::VALUE_ON : ui_colors::VALUE_OFF;
            ui.add_text(value_str, panel_x + panel_w - 80.0f, item_y + 10.0f, 1.0f, value_color);
        } else if (item.type == MenuItemType::Slider && item.slider_value) {
            std::string value_str;
            int idx = *item.slider_value - item.slider_min;
            if (!item.slider_labels.empty() && idx >= 0 && idx < static_cast<int>(item.slider_labels.size())) {
                value_str = item.slider_labels[idx];
            } else {
                char buf[16];
                snprintf(buf, sizeof(buf), "%d", *item.slider_value);
                value_str = buf;
            }

            std::string display = "< " + value_str + " >";
            ui.add_text(display, panel_x + panel_w - 120.0f, item_y + 10.0f, 1.0f, ui_colors::VALUE_SLIDER);
        } else if (item.type == MenuItemType::FloatSlider && item.float_value) {
            float slider_x = panel_x + panel_w - 200.0f;
            float slider_w = 120.0f;
            float slider_h = 8.0f;
            float slider_y_center = item_y + 18.0f;

            ui.add_filled_rect(slider_x, slider_y_center - slider_h/2, slider_w, slider_h, 0xFF444444);

            float fill_pct = (*item.float_value - item.float_min) / (item.float_max - item.float_min);
            ui.add_filled_rect(slider_x, slider_y_center - slider_h/2, slider_w * fill_pct, slider_h, ui_colors::VALUE_SLIDER);

            char value_buf[32];
            snprintf(value_buf, sizeof(value_buf), "%.2f", *item.float_value);
            ui.add_text(value_buf, panel_x + panel_w - 65.0f, item_y + 10.0f, 0.9f, ui_colors::WHITE);
        } else if (item.type == MenuItemType::Submenu) {
            ui.add_text(">", panel_x + panel_w - 40.0f, item_y + 10.0f, 1.0f, text_color);
        }

        item_y += 50.0f;
    }

    const char* hint = "W/S: Navigate  |  A/D: Adjust  |  SPACE: Select  |  ESC: Back";
    ui.add_text(hint, panel_x + 20.0f, panel_y + panel_h - 30.0f, 0.75f, ui_colors::TEXT_HINT);
}

} // namespace mmo
