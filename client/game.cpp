#include "game.hpp"
#include <iostream>
#include <chrono>
#include <unordered_set>
#include <random>
#include <cstring>

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
    // Initialize SDL
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        std::cerr << "Failed to initialize SDL: " << SDL_GetError() << std::endl;
        return false;
    }
    
    host_ = host;
    port_ = port;
    
    // Initialize renderer
    if (!renderer_.init(1280, 720, "MMO Client - Select Class")) {
        return false;
    }
    
    // Load 3D models - find assets path relative to executable
    std::string assets_path = "assets";
    // Try common paths
    if (!renderer_.load_models(assets_path)) {
        assets_path = "../assets";
        if (!renderer_.load_models(assets_path)) {
            assets_path = "../../assets";
            renderer_.load_models(assets_path);  // Last attempt, will fall back to 2D if fails
        }
    }
    
    // Set up network callback
    network_.set_message_callback(
        [this](MessageType type, const std::vector<uint8_t>& payload) {
            handle_network_message(type, payload);
        });
    
    running_ = true;
    game_state_ = GameState::ClassSelect;
    last_frame_time_ = SDL_GetTicks();
    fps_timer_ = last_frame_time_;
    
    return true;
}

void Game::run() {
    while (running_) {
        uint64_t current_time = SDL_GetTicks();
        float dt = (current_time - last_frame_time_) / 1000.0f;
        last_frame_time_ = current_time;
        
        // Clamp delta time to avoid huge jumps
        if (dt > 0.1f) dt = 0.1f;
        
        // FPS counter
        frame_count_++;
        if (current_time - fps_timer_ >= 1000) {
            fps_ = static_cast<float>(frame_count_);
            frame_count_ = 0;
            fps_timer_ = current_time;
        }
        
        // Process input
        if (!input_.process_events()) {
            running_ = false;
            break;
        }
        
        // Update and render based on game state
        update(dt);
        render();
    }
}

void Game::shutdown() {
    network_.disconnect();
    renderer_.shutdown();
    SDL_Quit();
}

void Game::update(float dt) {
    switch (game_state_) {
        case GameState::ClassSelect:
            update_class_select(dt);
            break;
        case GameState::Connecting:
            update_connecting(dt);
            break;
        case GameState::Playing:
            update_playing(dt);
            break;
    }
}

void Game::render() {
    switch (game_state_) {
        case GameState::ClassSelect:
            render_class_select();
            break;
        case GameState::Connecting:
            render_connecting();
            break;
        case GameState::Playing:
            render_playing();
            break;
    }
}

void Game::update_class_select(float dt) {
    (void)dt;
    
    // Handle class selection input
    auto& input_state = input_.get_input();
    static bool key_pressed = false;
    
    if (input_state.move_left && !key_pressed) {
        selected_class_index_ = (selected_class_index_ + 3) % 4;  // Wrap left
        key_pressed = true;
    } else if (input_state.move_right && !key_pressed) {
        selected_class_index_ = (selected_class_index_ + 1) % 4;  // Wrap right
        key_pressed = true;
    } else if (input_state.attacking && !key_pressed) {
        // Confirm selection with space
        local_player_class_ = static_cast<PlayerClass>(selected_class_index_);
        game_state_ = GameState::Connecting;
        connecting_timer_ = 0.0f;
        
        // Start connecting
        if (!network_.connect(host_, port_, player_name_, local_player_class_)) {
            std::cerr << "Failed to connect to server" << std::endl;
            game_state_ = GameState::ClassSelect;
        }
        key_pressed = true;
    }
    
    if (!input_state.move_left && !input_state.move_right && !input_state.attacking) {
        key_pressed = false;
    }
}

void Game::render_class_select() {
    // Set up camera for menu screen and update matrices
    renderer_.set_camera(WORLD_WIDTH / 2.0f, WORLD_HEIGHT / 2.0f);
    renderer_.set_camera_orbit(0.0f, 30.0f);
    renderer_.update_camera_smooth(0.016f);  // Use smooth with small dt for initialization
    
    renderer_.begin_frame();
    
    // Dark background
    renderer_.begin_ui();
    
    float center_x = renderer_.width() / 2.0f;
    float center_y = renderer_.height() / 2.0f;
    
    // Draw title area
    renderer_.draw_filled_rect(0, 0, renderer_.width(), 100, 0xFF332211);
    
    // Class selection boxes
    float box_size = 120.0f;
    float spacing = 150.0f;
    float start_x = center_x - spacing * 1.5f;
    float box_y = center_y - 50.0f;
    
    PlayerClass classes[] = {PlayerClass::Warrior, PlayerClass::Mage, PlayerClass::Paladin, PlayerClass::Archer};
    const char* class_names[] = {"WARRIOR", "MAGE", "PALADIN", "ARCHER"};
    const char* class_desc[] = {
        "High HP, Close Range\nSword Arc Attack",
        "Low HP, Long Range\nBeam Attack", 
        "Medium HP, AOE\nGround Consecration",
        "Low HP, Long Range\nPrecision Shots"
    };
    uint32_t class_colors[] = {0xFF5050C8, 0xFFC85050, 0xFF50B4C8, 0xFF50C850};
    
    for (int i = 0; i < 4; ++i) {
        float x = start_x + i * spacing;
        bool selected = (i == selected_class_index_);
        
        // Selection highlight
        if (selected) {
            renderer_.draw_filled_rect(x - box_size/2 - 10, box_y - box_size/2 - 10, 
                                       box_size + 20, box_size + 20, 0x40FFFFFF);
        }
        
        // Class preview
        renderer_.draw_class_preview(classes[i], x, box_y, box_size);
        
        // Class name below
        // (text rendering is placeholder, so we draw colored boxes as labels)
        uint32_t text_color = selected ? 0xFFFFFFFF : 0xFFAAAAAA;
        renderer_.draw_filled_rect(x - 50, box_y + box_size/2 + 20, 100, 25, class_colors[i]);
        
        (void)class_names;
        (void)class_desc;
        (void)text_color;
    }
    
    // Instructions
    renderer_.draw_filled_rect(center_x - 200, renderer_.height() - 80, 400, 40, 0x80000000);
    
    renderer_.end_ui();
    renderer_.end_frame();
}

void Game::update_connecting(float dt) {
    connecting_timer_ += dt;
    
    // Poll network
    network_.poll_messages();
    
    // Check if connected
    if (network_.is_connected() && local_player_id_ != 0) {
        game_state_ = GameState::Playing;
        std::cout << "Connected! Player ID: " << local_player_id_ << std::endl;
    }
    
    // Timeout after 10 seconds
    if (connecting_timer_ > 10.0f) {
        std::cerr << "Connection timeout" << std::endl;
        network_.disconnect();
        game_state_ = GameState::ClassSelect;
    }
}

void Game::render_connecting() {
    // Set up camera for menu screen and update matrices
    renderer_.set_camera(WORLD_WIDTH / 2.0f, WORLD_HEIGHT / 2.0f);
    renderer_.set_camera_orbit(0.0f, 30.0f);
    renderer_.update_camera_smooth(0.016f);
    
    renderer_.begin_frame();
    renderer_.begin_ui();
    
    float center_x = renderer_.width() / 2.0f;
    float center_y = renderer_.height() / 2.0f;
    
    // Loading indicator - spinning dots
    int num_dots = 8;
    float radius = 40.0f;
    float dot_radius = 8.0f;
    float angle_offset = connecting_timer_ * 3.0f;
    
    for (int i = 0; i < num_dots; ++i) {
        float angle = angle_offset + (i / static_cast<float>(num_dots)) * 2.0f * 3.14159f;
        float x = center_x + std::cos(angle) * radius;
        float y = center_y + std::sin(angle) * radius;
        uint8_t alpha = static_cast<uint8_t>(255 * (i + 1) / static_cast<float>(num_dots));
        renderer_.draw_filled_rect(x - dot_radius, y - dot_radius, 
                                   dot_radius * 2, dot_radius * 2, 
                                   0x00FFFFFF | (alpha << 24));
    }
    
    renderer_.end_frame();
}

void Game::update_playing(float dt) {
    // Check connection
    if (!network_.is_connected()) {
        std::cout << "Lost connection to server" << std::endl;
        running_ = false;
        return;
    }
    
    // Process network messages
    network_.poll_messages();
    
    // Update player screen position for mouse direction calculation (center of screen)
    input_.set_player_screen_pos(renderer_.width() / 2.0f, renderer_.height() / 2.0f);
    
    // Send input at reasonable rate (every frame but throttled by network)
    static float input_send_timer = 0.0f;
    input_send_timer += dt;
    if (input_send_timer >= 0.016f) {  // ~60 times per second max
        network_.send_input(input_.get_input());
        input_send_timer = 0.0f;
    }
    input_.reset_changed();
    
    // Update local player ID
    local_player_id_ = network_.local_player_id();
    
    // Run interpolation system
    interpolation_system_.update(registry_, dt);
    
    // Update attack effects
    update_attack_effects(dt);
    
    // Update skeletal animations
    renderer_.update_animations(dt);
    
    // Center camera on local player and pass velocity for look-ahead
    auto it = network_to_entity_.find(local_player_id_);
    if (it != network_to_entity_.end() && registry_.valid(it->second)) {
        auto& transform = registry_.get<ecs::Transform>(it->second);
        renderer_.set_camera(transform.x, transform.y);
        
        // Pass velocity to camera for intelligent look-ahead
        if (auto* vel = registry_.try_get<ecs::Velocity>(it->second)) {
            renderer_.set_camera_velocity(vel->x, vel->y);
        }
        
        // Check if player is in combat (attacking or recently attacked)
        if (auto* combat = registry_.try_get<ecs::Combat>(it->second)) {
            renderer_.set_in_combat(combat->is_attacking || combat->current_cooldown > 0);
        }
    }
    
    // Handle third-person camera controls - set yaw/pitch from input
    renderer_.set_camera_orbit(input_.get_camera_yaw(), input_.get_camera_pitch());
    
    // Handle sprint mode for camera
    renderer_.set_sprinting(input_.is_sprinting() && 
        (input_.move_forward() || input_.move_backward() || 
         input_.move_left() || input_.move_right()));
    
    // Handle zoom
    float zoom_delta = input_.get_camera_zoom_delta();
    if (zoom_delta != 0.0f) {
        renderer_.adjust_camera_zoom(zoom_delta);
    }
    input_.reset_camera_deltas();
    
    // Update camera with smooth interpolation
    renderer_.update_camera_smooth(dt);
    
    // Pass actual camera forward to input handler for correct movement direction
    // This accounts for shoulder offset so W always moves "into the screen"
    glm::vec3 cam_forward = renderer_.get_camera_system().get_forward();
    input_.set_camera_forward(cam_forward.x, cam_forward.z);
}

void Game::render_playing() {
    // === Shadow Pass ===
    // Render depth from light's perspective first
    renderer_.begin_shadow_pass();
    
    // Render mountain shadows (large distant mountains can cast shadows into playable area)
    renderer_.draw_mountain_shadows();
    
    // Render tree shadows
    renderer_.draw_tree_shadows();
    
    // Render all shadow-casting entities to shadow map
    auto shadow_view = registry_.view<ecs::NetworkId, ecs::Transform, ecs::Health, ecs::EntityInfo, ecs::Name>();
    for (auto entity : shadow_view) {
        auto& net_id = shadow_view.get<ecs::NetworkId>(entity);
        auto& transform = shadow_view.get<ecs::Transform>(entity);
        auto& health = shadow_view.get<ecs::Health>(entity);
        auto& info = shadow_view.get<ecs::EntityInfo>(entity);
        auto& name = shadow_view.get<ecs::Name>(entity);
        
        EntityState state;
        state.id = net_id.id;
        state.x = transform.x;
        state.y = transform.y;
        state.health = health.current;
        state.max_health = health.max;
        state.type = info.type;
        state.player_class = info.player_class;
        state.color = info.color;
        state.npc_type = info.npc_type;
        state.building_type = info.building_type;
        std::strncpy(state.name, name.value.c_str(), sizeof(state.name) - 1);
        
        if (auto* vel = registry_.try_get<ecs::Velocity>(entity)) {
            state.vx = vel->x;
            state.vy = vel->y;
        }
        if (auto* attack_dir = registry_.try_get<ecs::AttackDirection>(entity)) {
            state.attack_dir_x = attack_dir->x;
            state.attack_dir_y = attack_dir->y;
        }
        
        renderer_.draw_entity_shadow(state);
    }
    
    renderer_.end_shadow_pass();
    
    // === Main Render Pass ===
    renderer_.begin_frame();
    
    // Draw skybox backdrop first (behind everything)
    renderer_.draw_skybox();
    
    // Draw 3D distant mountains with fog
    renderer_.draw_distant_mountains();
    
    // Draw scattered rocks between player and mountains
    renderer_.draw_rocks();
    
    // Draw trees and forests
    renderer_.draw_trees();
    
    // Draw ground plane
    renderer_.draw_ground();
    // renderer_.draw_grid();  // Debug grid disabled - we have proper terrain now
    
    // Draw grass on top of ground
    renderer_.draw_grass();
    
    // Draw attack effects (behind entities)
    for (const auto& effect : attack_effects_) {
        renderer_.draw_attack_effect(effect);
    }
    
    // Draw all entities from ECS registry
    auto view = registry_.view<ecs::NetworkId, ecs::Transform, ecs::Health, ecs::EntityInfo, ecs::Name>();
    for (auto entity : view) {
        auto& net_id = view.get<ecs::NetworkId>(entity);
        auto& transform = view.get<ecs::Transform>(entity);
        auto& health = view.get<ecs::Health>(entity);
        auto& info = view.get<ecs::EntityInfo>(entity);
        auto& name = view.get<ecs::Name>(entity);
        
        // Build EntityState for renderer
        EntityState state;
        state.id = net_id.id;
        state.x = transform.x;
        state.y = transform.y;
        state.health = health.current;
        state.max_health = health.max;
        state.type = info.type;
        state.player_class = info.player_class;
        state.color = info.color;
        std::strncpy(state.name, name.value.c_str(), sizeof(state.name) - 1);
        
        if (auto* vel = registry_.try_get<ecs::Velocity>(entity)) {
            state.vx = vel->x;
            state.vy = vel->y;
        }
        
        if (auto* combat = registry_.try_get<ecs::Combat>(entity)) {
            state.is_attacking = combat->is_attacking;
            state.attack_cooldown = combat->current_cooldown;
        }
        
        if (auto* attack_dir = registry_.try_get<ecs::AttackDirection>(entity)) {
            state.attack_dir_x = attack_dir->x;
            state.attack_dir_y = attack_dir->y;
        }
        
        // Include npc_type and building_type for proper model selection
        state.npc_type = info.npc_type;
        state.building_type = info.building_type;
        
        bool is_local = (net_id.id == local_player_id_);
        renderer_.draw_entity(state, is_local);
    }
    
    // Draw UI elements (after 3D world)
    renderer_.begin_ui();
    
    // Draw target reticle for ranged classes (Archer and Mage)
    auto it = network_to_entity_.find(local_player_id_);
    if (local_player_class_ == PlayerClass::Archer || local_player_class_ == PlayerClass::Mage) {
        renderer_.draw_target_reticle();
    }
    
    // Draw player health bar in UI
    if (it != network_to_entity_.end() && registry_.valid(it->second)) {
        auto& health = registry_.get<ecs::Health>(it->second);
        float health_ratio = health.current / health.max;
        renderer_.draw_player_health_ui(health_ratio, health.max);
    }
    
    renderer_.end_ui();
    
    renderer_.end_frame();
}

void Game::handle_network_message(MessageType type, const std::vector<uint8_t>& payload) {
    switch (type) {
        case MessageType::ConnectionAccepted:
            on_connection_accepted(payload);
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
        std::memcpy(&local_player_id_, payload.data(), sizeof(local_player_id_));
        std::cout << "Connection accepted, player ID: " << local_player_id_ << std::endl;
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
        
        // Check if attack just started (for spawning effects)
        bool was_attacking = prev_attacking_[state.id];
        if (state.is_attacking && !was_attacking) {
            // Use attack direction from server (mouse direction)
            float dir_x = state.attack_dir_x;
            float dir_y = state.attack_dir_y;
            
            // Normalize just in case
            float len = std::sqrt(dir_x * dir_x + dir_y * dir_y);
            if (len < 0.001f) {
                dir_x = 0;
                dir_y = 1;
            } else {
                dir_x /= len;
                dir_y /= len;
            }
            
            spawn_attack_effect(state.id, state.player_class, state.x, state.y, dir_x, dir_y);
            
            // Camera shake when local player attacks
            if (state.id == local_player_id_) {
                renderer_.notify_player_attack();
            }
        }
        prev_attacking_[state.id] = state.is_attacking;
        
        entt::entity entity = find_or_create_entity(state.id);
        update_entity_from_state(entity, state);
    }
    
    // Remove entities not in update
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
    interp.target_x = state.x;
    interp.target_y = state.y;
    interp.alpha = 0.0f;
    
    velocity.x = state.vx;
    velocity.y = state.vy;
    
    health.current = state.health;
    health.max = state.max_health;
    
    info.type = state.type;
    info.player_class = state.player_class;
    info.npc_type = state.npc_type;
    info.building_type = state.building_type;
    info.color = state.color;
    
    name.value = state.name;
    
    combat.is_attacking = state.is_attacking;
    combat.current_cooldown = state.attack_cooldown;
    
    // Store attack direction for player rotation
    if (!registry_.all_of<ecs::AttackDirection>(entity)) {
        registry_.emplace<ecs::AttackDirection>(entity);
    }
    auto& attack_dir = registry_.get<ecs::AttackDirection>(entity);
    attack_dir.x = state.attack_dir_x;
    attack_dir.y = state.attack_dir_y;
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

void Game::spawn_attack_effect(uint32_t attacker_id, PlayerClass attacker_class, float x, float y, float dir_x, float dir_y) {
    ecs::AttackEffect effect;
    effect.attacker_class = attacker_class;
    effect.x = x;
    effect.y = y;
    effect.direction_x = dir_x;
    effect.direction_y = dir_y;
    
    switch (attacker_class) {
        case PlayerClass::Warrior:
            effect.duration = 0.3f;
            break;
        case PlayerClass::Mage:
            effect.duration = 0.4f;
            break;
        case PlayerClass::Paladin:
            effect.duration = 0.6f;
            // For paladin, target is in front of them
            effect.target_x = x + dir_x * PALADIN_ATTACK_RANGE * 0.5f;
            effect.target_y = y + dir_y * PALADIN_ATTACK_RANGE * 0.5f;
            break;
    }
    
    effect.timer = effect.duration;
    attack_effects_.push_back(effect);
    
    (void)attacker_id;
}

void Game::update_attack_effects(float dt) {
    // Update and remove expired effects
    for (auto it = attack_effects_.begin(); it != attack_effects_.end(); ) {
        it->timer -= dt;
        if (it->timer <= 0.0f) {
            it = attack_effects_.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace mmo
