#include "game.hpp"
#include "render_constants.hpp"
#include "common/heightmap.hpp"
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
    game_state_ = GameState::Connecting;
    connecting_timer_ = 0.0f;
    last_frame_time_ = SDL_GetTicks();
    fps_timer_ = last_frame_time_;

    // Initialize menu
    init_menu_items();

    // Connect to server immediately (class selection happens after receiving ClassList)
    if (!network_.connect(host_, port_, player_name_)) {
        std::cerr << "Failed to connect to server" << std::endl;
        return false;
    }

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
    // Handle menu first (available in class select and playing states)
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

void Game::render() {
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
    // Poll network (still receiving messages like heightmap)
    network_.poll_messages();

    // Animate highlight transition (8.0 = animation speed)
    class_select_highlight_progress_ = std::min(1.0f, class_select_highlight_progress_ + dt * 8.0f);

    // Reset animation on selection change
    if (selected_class_index_ != prev_class_selected_) {
        class_select_highlight_progress_ = 0.0f;
        prev_class_selected_ = selected_class_index_;
    }

    int num_classes = static_cast<int>(available_classes_.size());
    if (num_classes == 0) return;

    // Handle class selection input
    auto& input_state = input_.get_input();
    static bool key_pressed = false;

    if (input_state.move_left && !key_pressed) {
        selected_class_index_ = (selected_class_index_ + num_classes - 1) % num_classes;
        key_pressed = true;
    } else if (input_state.move_right && !key_pressed) {
        selected_class_index_ = (selected_class_index_ + 1) % num_classes;
        key_pressed = true;
    } else if (input_state.attacking && !key_pressed) {
        // Confirm selection — send ClassSelect to server
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
    // Set up camera for menu screen and update matrices
    renderer_.set_camera(world_config_.world_width / 2.0f, world_config_.world_height / 2.0f);
    renderer_.set_camera_orbit(0.0f, 30.0f);
    renderer_.update_camera_smooth(0.016f);  // Use smooth with small dt for initialization

    // Clear and populate scenes
    render_scene_.clear();
    ui_scene_.clear();

    // Build UI for class selection
    build_class_select_ui(ui_scene_);
    
    // Add menu overlay if open
    if (menu_open_) {
        build_menu_ui(ui_scene_);
    }
    
    // Render using scene-based API
    renderer_.begin_frame();
    renderer_.begin_ui();
    renderer_.render_ui(ui_scene_);
    renderer_.end_ui();
    renderer_.end_frame();
}

void Game::update_connecting(float dt) {
    connecting_timer_ += dt;

    // Poll network
    network_.poll_messages();

    // Transition to ClassSelect happens in on_class_list() when ClassList is received

    // Timeout after 10 seconds
    if (connecting_timer_ > 10.0f) {
        std::cerr << "Connection timeout" << std::endl;
        network_.disconnect();
        running_ = false;
    }
}

void Game::update_spawning(float dt) {
    connecting_timer_ += dt;

    // Poll network
    network_.poll_messages();

    // Transition to Playing happens in on_connection_accepted() when we get a non-zero player ID

    // Timeout after 10 seconds
    if (connecting_timer_ > 10.0f) {
        std::cerr << "Spawn timeout" << std::endl;
        network_.disconnect();
        running_ = false;
    }
}

void Game::render_spawning() {
    render_connecting();  // Reuse the same connecting UI
}

void Game::render_connecting() {
    // Set up camera for menu screen and update matrices
    renderer_.set_camera(world_config_.world_width / 2.0f, world_config_.world_height / 2.0f);
    renderer_.set_camera_orbit(0.0f, 30.0f);
    renderer_.update_camera_smooth(0.016f);

    // Clear and populate scenes
    render_scene_.clear();
    ui_scene_.clear();
    
    // Build UI for connecting screen
    build_connecting_ui(ui_scene_);
    
    // Render using scene-based API
    renderer_.begin_frame();
    renderer_.begin_ui();
    renderer_.render_ui(ui_scene_);
    renderer_.end_ui();
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
    // Clear and populate scenes
    render_scene_.clear();
    ui_scene_.clear();
    
    // Configure world rendering flags based on graphics settings
    render_scene_.set_draw_skybox(graphics_settings_.skybox_enabled);
    render_scene_.set_draw_mountains(graphics_settings_.mountains_enabled);
    render_scene_.set_draw_rocks(graphics_settings_.rocks_enabled);
    render_scene_.set_draw_trees(graphics_settings_.trees_enabled);
    render_scene_.set_draw_ground(true);  // Always draw ground
    render_scene_.set_draw_grass(graphics_settings_.grass_enabled);
    // Add attack effects to scene
    for (const auto& effect : attack_effects_) {
        render_scene_.add_effect(effect);
    }
    
    // Collect entities using RenderSystem
    render_system_.collect_entities(registry_, render_scene_, local_player_id_);
    
    // Build UI for playing state
    build_playing_ui(ui_scene_);
    
    // Add menu overlay if open
    if (menu_open_) {
        build_menu_ui(ui_scene_);
    }
    
    // Render using scene-based API
    renderer_.render(render_scene_, ui_scene_);
}

void Game::handle_network_message(MessageType type, const std::vector<uint8_t>& payload) {
    switch (type) {
        case MessageType::ConnectionAccepted:
            on_connection_accepted(payload);
            break;
        case MessageType::WorldConfig:
            if (payload.size() >= NetWorldConfig::serialized_size()) {
                world_config_.deserialize(payload.data());
                world_config_received_ = true;
                interpolation_system_.set_interpolation_time(1.0f / world_config_.tick_rate);
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
            // Initial connection accepted — waiting for class select
            std::cout << "Connection accepted, waiting for class list..." << std::endl;
        } else {
            // Real player ID after class selection
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
        
        // Pass heightmap to renderer for GPU texture upload
        renderer_.set_heightmap(*heightmap_);
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
            
            spawn_attack_effect(state, dir_x, dir_y);
            
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
    interp.prev_z = transform.z;
    interp.target_x = state.x;
    interp.target_y = state.y;
    interp.target_z = state.z;  // Server-provided terrain height
    interp.alpha = 0.0f;
    
    // Update rotation (used for buildings and environment objects)
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
    
    // Store attack direction for player rotation
    if (!registry_.all_of<ecs::AttackDirection>(entity)) {
        registry_.emplace<ecs::AttackDirection>(entity);
    }
    auto& attack_dir = registry_.get<ecs::AttackDirection>(entity);
    attack_dir.x = state.attack_dir_x;
    attack_dir.y = state.attack_dir_y;
    
    // Store scale (used for environment objects)
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
    effect.range = state.cone_angle;  // Server sends attack range in cone_angle for effect scaling
    effect.cone_angle = state.cone_angle;

    // For orbit effects, target is in front of attacker
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
    
    // Submenu buttons
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
    
    // Resume button
    MenuItem resume_item;
    resume_item.label = "Resume Game";
    resume_item.type = MenuItemType::Button;
    resume_item.action = [this]() { 
        menu_open_ = false;
        input_.set_game_input_enabled(true);
    };
    menu_items_.push_back(resume_item);
    
    // Quit button
    MenuItem quit_item;
    quit_item.label = "Quit to Desktop";
    quit_item.type = MenuItemType::Button;
    quit_item.action = [this]() { 
        running_ = false;
    };
    menu_items_.push_back(quit_item);
}

void Game::init_controls_menu() {
    menu_items_.clear();
    menu_selected_index_ = 0;
    
    // Mouse sensitivity
    MenuItem mouse_sens;
    mouse_sens.label = "Mouse Sensitivity";
    mouse_sens.type = MenuItemType::FloatSlider;
    mouse_sens.float_value = &controls_settings_.mouse_sensitivity;
    mouse_sens.float_min = 0.05f;
    mouse_sens.float_max = 1.0f;
    mouse_sens.float_step = 0.05f;
    menu_items_.push_back(mouse_sens);
    
    // Controller sensitivity
    MenuItem ctrl_sens;
    ctrl_sens.label = "Controller Sensitivity";
    ctrl_sens.type = MenuItemType::FloatSlider;
    ctrl_sens.float_value = &controls_settings_.controller_sensitivity;
    ctrl_sens.float_min = 0.5f;
    ctrl_sens.float_max = 5.0f;
    ctrl_sens.float_step = 0.25f;
    menu_items_.push_back(ctrl_sens);
    
    // Camera inversion
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
    
    // Back button
    MenuItem back_item;
    back_item.label = "< Back";
    back_item.type = MenuItemType::Submenu;
    back_item.target_page = MenuPage::Main;
    menu_items_.push_back(back_item);
}

void Game::init_graphics_menu() {
    menu_items_.clear();
    menu_selected_index_ = 0;
    
    // Graphics settings toggles
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
    
    // Anisotropic filtering
    MenuItem aniso;
    aniso.label = "Anisotropic Filter";
    aniso.type = MenuItemType::Slider;
    aniso.slider_value = &graphics_settings_.anisotropic_filter;
    aniso.slider_min = 0;
    aniso.slider_max = 4;
    aniso.slider_labels = {"Off", "2x", "4x", "8x", "16x"};
    menu_items_.push_back(aniso);
    
    // VSync mode
    MenuItem vsync;
    vsync.label = "VSync";
    vsync.type = MenuItemType::Slider;
    vsync.slider_value = &graphics_settings_.vsync_mode;
    vsync.slider_min = 0;
    vsync.slider_max = 2;
    vsync.slider_labels = {"Off", "Double Buffer", "Triple Buffer"};
    menu_items_.push_back(vsync);
    
    // FPS counter
    MenuItem fps_counter;
    fps_counter.label = "Show FPS";
    fps_counter.type = MenuItemType::Toggle;
    fps_counter.toggle_value = &graphics_settings_.show_fps;
    menu_items_.push_back(fps_counter);
    
    // Back button
    MenuItem back_item;
    back_item.label = "< Back";
    back_item.type = MenuItemType::Submenu;
    back_item.target_page = MenuPage::Main;
    menu_items_.push_back(back_item);
}

void Game::update_menu(float dt) {
    // Animate highlight transition (8.0 = animation speed)
    menu_highlight_progress_ = std::min(1.0f, menu_highlight_progress_ + dt * 8.0f);

    // Reset animation when selection changes
    if (menu_selected_index_ != prev_menu_selected_) {
        menu_highlight_progress_ = 0.0f;
        prev_menu_selected_ = menu_selected_index_;
    }

    // Handle menu toggle
    if (input_.menu_toggle_pressed()) {
        if (current_menu_page_ != MenuPage::Main) {
            // Go back to main menu
            current_menu_page_ = MenuPage::Main;
            init_main_menu();
        } else {
            // Toggle menu open/closed
            menu_open_ = !menu_open_;
            input_.set_game_input_enabled(!menu_open_);
        }
        input_.clear_menu_inputs();
        return;
    }

    if (!menu_open_) return;

    // Navigation
    if (input_.menu_up_pressed()) {
        menu_selected_index_ = (menu_selected_index_ - 1 + static_cast<int>(menu_items_.size())) % static_cast<int>(menu_items_.size());
    }
    if (input_.menu_down_pressed()) {
        menu_selected_index_ = (menu_selected_index_ + 1) % static_cast<int>(menu_items_.size());
    }
    
    // Selection/toggle
    MenuItem& item = menu_items_[menu_selected_index_];
    if (item.type == MenuItemType::Toggle) {
        if (input_.menu_select_pressed() || input_.menu_left_pressed() || input_.menu_right_pressed()) {
            if (item.toggle_value) {
                *item.toggle_value = !*item.toggle_value;
                apply_graphics_settings();
                apply_controls_settings();
            }
        }
    } else if (item.type == MenuItemType::Slider) {
        if (item.slider_value) {
            if (input_.menu_left_pressed()) {
                *item.slider_value = std::max(item.slider_min, *item.slider_value - 1);
                apply_graphics_settings();
            }
            if (input_.menu_right_pressed()) {
                *item.slider_value = std::min(item.slider_max, *item.slider_value + 1);
                apply_graphics_settings();
            }
        }
    } else if (item.type == MenuItemType::FloatSlider) {
        if (item.float_value) {
            if (input_.menu_left_pressed()) {
                *item.float_value = std::max(item.float_min, *item.float_value - item.float_step);
                apply_controls_settings();
            }
            if (input_.menu_right_pressed()) {
                *item.float_value = std::min(item.float_max, *item.float_value + item.float_step);
                apply_controls_settings();
            }
        }
    } else if (item.type == MenuItemType::Button) {
        if (input_.menu_select_pressed()) {
            if (item.action) {
                item.action();
            }
        }
    } else if (item.type == MenuItemType::Submenu) {
        if (input_.menu_select_pressed()) {
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
    
    input_.clear_menu_inputs();
}

void Game::apply_graphics_settings() {
    renderer_.set_fog_enabled(graphics_settings_.fog_enabled);
    renderer_.set_grass_enabled(graphics_settings_.grass_enabled);
    renderer_.set_skybox_enabled(graphics_settings_.skybox_enabled);
    renderer_.set_mountains_enabled(graphics_settings_.mountains_enabled);
    renderer_.set_trees_enabled(graphics_settings_.trees_enabled);
    renderer_.set_rocks_enabled(graphics_settings_.rocks_enabled);
    renderer_.set_anisotropic_filter(graphics_settings_.anisotropic_filter);
    renderer_.set_vsync_mode(graphics_settings_.vsync_mode);
}

void Game::apply_controls_settings() {
    input_.set_mouse_sensitivity(controls_settings_.mouse_sensitivity);
    input_.set_controller_sensitivity(controls_settings_.controller_sensitivity);
    input_.set_camera_x_inverted(controls_settings_.invert_camera_x);
    input_.set_camera_y_inverted(controls_settings_.invert_camera_y);
}

// ============================================================================
// Scene Building Helpers - Populate UI scenes without direct renderer calls
// ============================================================================

void Game::build_class_select_ui(UIScene& ui) {
    float center_x = renderer_.width() / 2.0f;
    float center_y = renderer_.height() / 2.0f;
    int num_classes = static_cast<int>(available_classes_.size());
    if (num_classes == 0) return;

    // Draw title area background
    ui.add_filled_rect(0, 0, static_cast<float>(renderer_.width()), 100, ui_colors::TITLE_BG);

    // Title text
    ui.add_text("SELECT YOUR CLASS", center_x - 150.0f, 30.0f, 2.0f, ui_colors::WHITE);

    // Subtitle
    ui.add_text("Use A/D to select, SPACE to confirm", center_x - 160.0f, 70.0f, 1.0f, ui_colors::TEXT_DIM);

    // Class selection boxes
    float box_size = 120.0f;
    float spacing = 150.0f;
    float start_x = center_x - spacing * (num_classes - 1) / 2.0f;
    float box_y = center_y - 50.0f;

    for (int i = 0; i < num_classes; ++i) {
        const auto& cls = available_classes_[i];
        float x = start_x + i * spacing;
        bool selected = (i == selected_class_index_);

        // Selection highlight
        if (selected) {
            ui.add_filled_rect(x - box_size/2 - 10, box_y - box_size/2 - 10,
                              box_size + 20, box_size + 20, ui_colors::SELECTION);
            ui.add_rect_outline(x - box_size/2 - 10, box_y - box_size/2 - 10,
                               box_size + 20, box_size + 20, ui_colors::WHITE, 3.0f);
        }

        // Class preview background (use select_color from server)
        ui.add_filled_rect(x - box_size/2, box_y - box_size/2, box_size, box_size, cls.select_color);

        // Class preview (use color from server)
        float half = box_size / 2.0f;
        ui.add_filled_rect(x - half, box_y - half, box_size, box_size, cls.color);
        ui.add_rect_outline(x - half, box_y - half, box_size, box_size, ui_colors::WHITE, 2.0f);

        // Class name below
        uint32_t text_color = selected ? ui_colors::WHITE : ui_colors::TEXT_DIM;
        float name_x = x - 40.0f;
        ui.add_text(cls.name, name_x, box_y + box_size/2 + 15.0f, 1.0f, text_color);

        // Class description
        ui.add_text(cls.short_desc, x - 55.0f, box_y + box_size/2 + 40.0f, 0.8f, ui_colors::TEXT_DIM);
    }

    // Selected class info panel
    const auto& sel = available_classes_[selected_class_index_];
    ui.add_filled_rect(center_x - 200, renderer_.height() - 120, 400, 80, ui_colors::PANEL_BG);
    ui.add_rect_outline(center_x - 200, renderer_.height() - 120, 400, 80, sel.select_color, 2.0f);

    ui.add_text(sel.desc_line1, center_x - 180.0f, renderer_.height() - 105.0f, 0.9f, ui_colors::WHITE);
    ui.add_text(sel.desc_line2, center_x - 180.0f, renderer_.height() - 80.0f, 0.9f, ui_colors::WHITE);

    // Controls hint
    ui.add_text("Press ESC anytime to open Settings Menu", center_x - 150.0f, renderer_.height() - 25.0f, 0.8f, ui_colors::TEXT_HINT);
}

void Game::build_connecting_ui(UIScene& ui) {
    float center_x = renderer_.width() / 2.0f;
    float center_y = renderer_.height() / 2.0f;
    
    // Background panel
    ui.add_filled_rect(center_x - 200, center_y - 100, 400, 200, ui_colors::PANEL_BG);
    ui.add_rect_outline(center_x - 200, center_y - 100, 400, 200, ui_colors::WHITE, 2.0f);

    // Title
    ui.add_text("CONNECTING", center_x - 80.0f, center_y - 80.0f, 1.5f, ui_colors::WHITE);
    
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
        ui.add_filled_rect(x - dot_radius, y - dot_radius, 
                          dot_radius * 2, dot_radius * 2, 
                          0x00FFFFFF | (alpha << 24));
    }
    
    // Connection info
    std::string connect_msg = "Connecting to " + host_ + ":" + std::to_string(port_);
    ui.add_text(connect_msg, center_x - 120.0f, center_y + 60.0f, 0.8f, ui_colors::TEXT_DIM);
}

void Game::build_playing_ui(UIScene& ui) {
    // Draw target reticle if server says this class shows one
    if (selected_class_index_ >= 0 && selected_class_index_ < static_cast<int>(available_classes_.size()) &&
        available_classes_[selected_class_index_].shows_reticle) {
        ui.add_target_reticle();
    }
    
    // Draw player health bar in UI
    auto it = network_to_entity_.find(local_player_id_);
    if (it != network_to_entity_.end() && registry_.valid(it->second)) {
        auto& health = registry_.get<ecs::Health>(it->second);
        float health_ratio = health.current / health.max;
        ui.add_player_health_bar(health_ratio, health.max);
    }
    
    // Draw FPS counter if enabled
    if (graphics_settings_.show_fps) {
        char fps_text[32];
        snprintf(fps_text, sizeof(fps_text), "FPS: %.0f", fps_);
        ui.add_text(fps_text, 10.0f, 10.0f, 1.0f, ui_colors::FPS_TEXT);
    }
}

void Game::build_menu_ui(UIScene& ui) {
    if (!menu_open_) return;
    
    float screen_w = static_cast<float>(renderer_.width());
    float screen_h = static_cast<float>(renderer_.height());
    
    // Menu panel
    float panel_w = 550.0f;
    float panel_h = 70.0f + menu_items_.size() * 50.0f + 50.0f;
    float panel_x = (screen_w - panel_w) / 2.0f;
    float panel_y = (screen_h - panel_h) / 2.0f;
    
    // Panel background
    ui.add_filled_rect(panel_x, panel_y, panel_w, panel_h, ui_colors::MENU_BG);
    ui.add_rect_outline(panel_x, panel_y, panel_w, panel_h, ui_colors::WHITE, 2.0f);
    
    // Title based on current page
    const char* title = "SETTINGS";
    switch (current_menu_page_) {
        case MenuPage::Main: title = "SETTINGS"; break;
        case MenuPage::Controls: title = "CONTROLS"; break;
        case MenuPage::Graphics: title = "GRAPHICS"; break;
    }
    ui.add_text(title, panel_x + panel_w / 2.0f - 60.0f, panel_y + 15.0f, 1.5f, ui_colors::WHITE);
    
    // Menu items
    float item_y = panel_y + 70.0f;
    for (size_t i = 0; i < menu_items_.size(); ++i) {
        const MenuItem& item = menu_items_[i];
        bool selected = (static_cast<int>(i) == menu_selected_index_);
        
        // Selection highlight
        if (selected) {
            ui.add_filled_rect(panel_x + 10.0f, item_y, panel_w - 20.0f, 40.0f, ui_colors::SELECTION);
        }

        // Item label
        uint32_t text_color = selected ? ui_colors::WHITE : ui_colors::TEXT_DIM;
        ui.add_text(item.label, panel_x + 30.0f, item_y + 10.0f, 1.0f, text_color);
        
        // Value display based on type
        if (item.type == MenuItemType::Toggle && item.toggle_value) {
            std::string value_str = *item.toggle_value ? "ON" : "OFF";
            uint32_t value_color = *item.toggle_value ? ui_colors::VALUE_ON : ui_colors::VALUE_OFF;
            ui.add_text(value_str, panel_x + panel_w - 80.0f, item_y + 10.0f, 1.0f, value_color);
        } else if (item.type == MenuItemType::Slider && item.slider_value) {
            // Display with < > arrows and label
            std::string value_str;
            int idx = *item.slider_value - item.slider_min;
            if (!item.slider_labels.empty() && idx >= 0 && idx < static_cast<int>(item.slider_labels.size())) {
                value_str = item.slider_labels[idx];
            } else {
                char buf[16];
                snprintf(buf, sizeof(buf), "%d", *item.slider_value);
                value_str = buf;
            }
            
            // Draw arrows if adjustable
            std::string display = "< " + value_str + " >";
            ui.add_text(display, panel_x + panel_w - 120.0f, item_y + 10.0f, 1.0f, ui_colors::VALUE_SLIDER);
        } else if (item.type == MenuItemType::FloatSlider && item.float_value) {
            // Draw slider bar
            float slider_x = panel_x + panel_w - 200.0f;
            float slider_w = 120.0f;
            float slider_h = 8.0f;
            float slider_y_center = item_y + 18.0f;
            
            // Background bar
            ui.add_filled_rect(slider_x, slider_y_center - slider_h/2, slider_w, slider_h, 0xFF444444);
            
            // Filled portion
            float fill_pct = (*item.float_value - item.float_min) / (item.float_max - item.float_min);
            ui.add_filled_rect(slider_x, slider_y_center - slider_h/2, slider_w * fill_pct, slider_h, ui_colors::VALUE_SLIDER);
            
            // Value text
            char value_buf[32];
            snprintf(value_buf, sizeof(value_buf), "%.2f", *item.float_value);
            ui.add_text(value_buf, panel_x + panel_w - 65.0f, item_y + 10.0f, 0.9f, ui_colors::WHITE);
        } else if (item.type == MenuItemType::Submenu) {
            // Draw arrow indicator
            ui.add_text(">", panel_x + panel_w - 40.0f, item_y + 10.0f, 1.0f, text_color);
        }
        
        item_y += 50.0f;
    }
    
    // Controls hint
    const char* hint = "W/S: Navigate  |  A/D: Adjust  |  SPACE: Select  |  ESC: Back";
    ui.add_text(hint, panel_x + 20.0f, panel_y + panel_h - 30.0f, 0.75f, ui_colors::TEXT_HINT);
}

} // namespace mmo
