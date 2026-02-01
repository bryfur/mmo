#include "editor_application.hpp"
#include "protocol/protocol.hpp"
#include "protocol/heightmap.hpp"
#include "server/heightmap_generator.hpp"
#include "engine/model_loader.hpp"
#include "engine/heightmap.hpp"
#include <iostream>
#include <SDL3/SDL.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_mouse.h>
#include <glm/gtc/matrix_transform.hpp>

namespace mmo::editor {

using namespace mmo::client::ecs;
using namespace mmo::protocol;

EditorApplication::EditorApplication() = default;
EditorApplication::~EditorApplication() = default;

bool EditorApplication::init() {
    if (!init_engine()) {
        std::cerr << "Failed to initialize engine" << std::endl;
        return false;
    }

    if (!on_init()) {
        std::cerr << "Failed to initialize editor application" << std::endl;
        return false;
    }

    return true;
}

bool EditorApplication::on_init() {
    // Load game configuration
    std::cout << "Loading game configuration..." << std::endl;
    if (!config_.load("data")) {
        std::cerr << "Failed to load game config from data/" << std::endl;
        return false;
    }

    // Initialize renderer
    std::cout << "Initializing renderer..." << std::endl;
    if (!init_renderer(1280, 720, "MMO Editor",
                      config_.world().width, config_.world().height)) {
        std::cerr << "Failed to initialize renderer" << std::endl;
        return false;
    }

    // Generate and set heightmap
    std::cout << "Generating heightmap..." << std::endl;
    protocol::HeightmapChunk heightmap;
    server::heightmap_init(heightmap, 0, 0, protocol::heightmap_config::CHUNK_RESOLUTION);
    server::heightmap_generator::generate_procedural(heightmap, config_.world().width, config_.world().height);

    engine::Heightmap engine_hm;
    engine_hm.resolution = heightmap.resolution;
    engine_hm.world_origin_x = heightmap.world_origin_x;
    engine_hm.world_origin_z = heightmap.world_origin_z;
    engine_hm.world_size = heightmap.world_size;
    engine_hm.min_height = protocol::heightmap_config::MIN_HEIGHT;
    engine_hm.max_height = protocol::heightmap_config::MAX_HEIGHT;
    engine_hm.height_data = heightmap.height_data;

    set_heightmap(engine_hm);
    std::cout << "Heightmap set: " << heightmap.resolution << "x" << heightmap.resolution << std::endl;

    // Load 3D models
    std::cout << "Loading 3D models..." << std::endl;
    if (!load_models("assets")) {
        std::cerr << "Warning: Some models failed to load" << std::endl;
    }

    // Load entities from configuration
    std::cout << "Loading entities from configuration..." << std::endl;
    load_entities_from_config();

    std::cout << "Editor initialized successfully" << std::endl;
    std::cout << "Controls:" << std::endl;
    std::cout << "  WASD - Move camera horizontally" << std::endl;
    std::cout << "  Q/E - Move camera up/down" << std::endl;
    std::cout << "  Mouse - Look around" << std::endl;
    std::cout << "  Shift - Slow movement" << std::endl;
    std::cout << "  Ctrl - Fast movement" << std::endl;
    std::cout << "  F1 - Toggle help" << std::endl;
    std::cout << "  ESC - Quit" << std::endl;

    return true;
}

void EditorApplication::shutdown() {
    on_shutdown();
    shutdown_engine();
}

void EditorApplication::on_shutdown() {
    shutdown_renderer();
}

void EditorApplication::on_update(float dt) {
    handle_camera_input(dt);
    camera_.update(dt);

    // Get keyboard state for key presses
    const bool* keys = SDL_GetKeyboardState(nullptr);
    static bool esc_was_pressed = false;
    static bool f1_was_pressed = false;

    // ESC to quit
    if (keys[SDL_SCANCODE_ESCAPE] && !esc_was_pressed) {
        quit();
    }
    esc_was_pressed = keys[SDL_SCANCODE_ESCAPE];

    // F1 to toggle help
    if (keys[SDL_SCANCODE_F1] && !f1_was_pressed) {
        show_help_ = !show_help_;
    }
    f1_was_pressed = keys[SDL_SCANCODE_F1];
}

void EditorApplication::on_render() {
    render_scene_.clear();
    ui_scene_.clear();

    // Build render scene from entities
    build_render_scene();

    // Build UI
    if (show_help_) {
        ui_scene_.add_text("MMO Editor - Help", 20, 20, 1.0f, 0xFFFFFFFF);
        ui_scene_.add_text("Camera: WASD=Move, Q/E=Up/Down, Mouse=Look", 20, 50, 1.0f, 0xFFCCCCCC);
        ui_scene_.add_text("Shift=Slow, Ctrl=Fast, ESC=Quit", 20, 70, 1.0f, 0xFFCCCCCC);
        ui_scene_.add_text("F1=Toggle Help", 20, 90, 1.0f, 0xFFCCCCCC);
    }

    // Add status bar
    {
        auto pos = camera_.get_position();
        char buf[256];
        snprintf(buf, sizeof(buf), "Camera: (%.1f, %.1f, %.1f) | FPS: %.1f",
                pos.x, pos.y, pos.z, fps());
        ui_scene_.add_text(buf, 20, screen_height() - 30, 1.0f, 0xFFCCCCCC);
    }

    // Render frame
    auto camera_state = get_camera_state();
    render_frame(render_scene_, ui_scene_, camera_state, 0.016f);
}

void EditorApplication::load_entities_from_config() {
    // Spawn buildings
    for (const auto& building : config_.buildings()) {
        spawn_building(building);
    }

    // Spawn town NPCs
    for (const auto& npc : config_.town_npcs()) {
        spawn_town_npc(npc);
    }

    // Count entities
    size_t entity_count = 0;
    auto view = registry_.view<Transform>();
    for (auto entity : view) {
        (void)entity;
        entity_count++;
    }
    std::cout << "Loaded " << entity_count << " entities from configuration" << std::endl;
}

void EditorApplication::spawn_building(const server::BuildingConfig& config) {
    auto entity = registry_.create();

    // Transform: Note that config y is world z coordinate
    float world_x = config.x + config_.world().width / 2.0f;
    float world_z = config.y + config_.world().height / 2.0f;
    float world_y = get_terrain_height(world_x, world_z);

    registry_.emplace<Transform>(entity, world_x, world_y, world_z,
                                 glm::radians(config.rotation));

    // Entity info
    EntityInfo info;
    info.type = EntityType::Building;
    info.model_name = config.model;
    info.target_size = config.target_size;
    info.color = 0xFFBB9977;  // Brownish
    registry_.emplace<EntityInfo>(entity, info);

    // Name
    registry_.emplace<Name>(entity, config.name);
}

void EditorApplication::spawn_town_npc(const server::TownNPCConfig& config) {
    auto entity = registry_.create();

    // Transform
    float world_x = config.x + config_.world().width / 2.0f;
    float world_z = config.y + config_.world().height / 2.0f;
    float world_y = get_terrain_height(world_x, world_z);

    registry_.emplace<Transform>(entity, world_x, world_y, world_z, 0.0f);

    // Entity info
    EntityInfo info;
    info.type = EntityType::TownNPC;
    info.model_name = config.model;
    info.target_size = 32.0f;
    info.color = config.color;
    registry_.emplace<EntityInfo>(entity, info);

    // Name
    registry_.emplace<Name>(entity, config.name);
}

void EditorApplication::build_render_scene() {
    // Add all entities to the render scene
    auto view = registry_.view<Transform, EntityInfo>();
    for (auto entity : view) {
        add_entity_to_scene(entity);
    }
}

void EditorApplication::add_entity_to_scene(entt::entity entity) {
    auto& transform = registry_.get<Transform>(entity);
    auto& info = registry_.get<EntityInfo>(entity);

    // Get model to access bounds
    auto* model_ptr = models().get_model(info.model_name);
    if (!model_ptr) {
        return;  // Skip if model not loaded
    }

    // Build transform matrix
    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, glm::vec3(transform.x, transform.y, transform.z));
    model = glm::rotate(model, transform.rotation, glm::vec3(0.0f, 1.0f, 0.0f));

    // Calculate proper scale using model dimensions
    float target_size = info.target_size;
    float model_size = model_ptr->max_dimension();
    float scale = (target_size * 1.5f) / model_size;
    model = glm::scale(model, glm::vec3(scale, scale, scale));

    // Center the model on its base
    float cx = (model_ptr->min_x + model_ptr->max_x) / 2.0f;
    float cy = model_ptr->min_y;  // Use minimum Y (bottom of model)
    float cz = (model_ptr->min_z + model_ptr->max_z) / 2.0f;
    model = model * glm::translate(glm::mat4(1.0f), glm::vec3(-cx, -cy, -cz));

    // Convert color from ARGB to vec4 (RGBA)
    uint32_t argb = info.color;
    float a = ((argb >> 24) & 0xFF) / 255.0f;
    float r = ((argb >> 16) & 0xFF) / 255.0f;
    float g = ((argb >> 8) & 0xFF) / 255.0f;
    float b = (argb & 0xFF) / 255.0f;
    glm::vec4 tint(r, g, b, a);

    // Add to render scene
    render_scene_.add_model(info.model_name, model, tint);
}

engine::scene::CameraState EditorApplication::get_camera_state() const {
    engine::scene::CameraState state;

    // Calculate aspect ratio
    float aspect = static_cast<float>(screen_width()) / static_cast<float>(screen_height());

    // Build matrices
    state.view = camera_.get_view_matrix();
    state.projection = camera_.get_projection_matrix(aspect);
    state.view_projection = state.projection * state.view;
    state.position = camera_.get_position();

    return state;
}

void EditorApplication::handle_camera_input(float dt) {
    // Get keyboard state using SDL directly
    const bool* keys = SDL_GetKeyboardState(nullptr);

    // Camera movement speed
    float move_speed = camera_.get_move_speed();
    if (keys[SDL_SCANCODE_LSHIFT]) {
        move_speed *= 0.3f;  // Slow mode
    }
    if (keys[SDL_SCANCODE_LCTRL]) {
        move_speed *= 3.0f;  // Fast mode
    }

    // WASD movement (camera-relative horizontal)
    if (keys[SDL_SCANCODE_W]) {
        camera_.move_forward(move_speed * dt);
    }
    if (keys[SDL_SCANCODE_S]) {
        camera_.move_forward(-move_speed * dt);
    }
    if (keys[SDL_SCANCODE_A]) {
        camera_.move_right(-move_speed * dt);
    }
    if (keys[SDL_SCANCODE_D]) {
        camera_.move_right(move_speed * dt);
    }

    // Q/E for up/down (world-space vertical)
    if (keys[SDL_SCANCODE_Q]) {
        camera_.move_up(-move_speed * dt);
    }
    if (keys[SDL_SCANCODE_E]) {
        camera_.move_up(move_speed * dt);
    }

    // Mouse look using relative mouse mode
    float mouse_dx = 0.0f, mouse_dy = 0.0f;
    SDL_GetRelativeMouseState(&mouse_dx, &mouse_dy);
    float sensitivity = 0.003f;
    camera_.rotate_yaw(mouse_dx * sensitivity);
    camera_.rotate_pitch(-mouse_dy * sensitivity);  // Invert Y
}

bool EditorApplication::load_models(const std::string& assets_path) {
    auto& mdl = models();
    std::string models_path = assets_path + "/models/";

    bool success = true;

    // Player models
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

    // NPC models
    success &= mdl.load_model("npc_enemy", models_path + "npc_enemy.glb");
    success &= mdl.load_model("npc_merchant", models_path + "npc_merchant.glb");
    success &= mdl.load_model("npc_guard", models_path + "npc_guard.glb");
    success &= mdl.load_model("npc_blacksmith", models_path + "npc_blacksmith.glb");
    success &= mdl.load_model("npc_innkeeper", models_path + "npc_innkeeper.glb");
    success &= mdl.load_model("npc_villager", models_path + "npc_villager.glb");

    // Building models
    success &= mdl.load_model("building_tavern", models_path + "building_tavern.glb");
    success &= mdl.load_model("building_blacksmith", models_path + "building_blacksmith.glb");
    success &= mdl.load_model("building_tower", models_path + "building_tower.glb");
    success &= mdl.load_model("building_shop", models_path + "building_shop.glb");
    success &= mdl.load_model("building_well", models_path + "building_well.glb");
    success &= mdl.load_model("building_house", models_path + "building_house.glb");
    success &= mdl.load_model("building_inn", models_path + "inn.glb");
    success &= mdl.load_model("wooden_log", models_path + "wooden_log.glb");
    success &= mdl.load_model("log_tower", models_path + "log_tower.glb");

    // Environment models
    success &= mdl.load_model("rock_boulder", models_path + "rock_boulder.glb");
    success &= mdl.load_model("rock_slate", models_path + "rock_slate.glb");
    success &= mdl.load_model("rock_spire", models_path + "rock_spire.glb");
    success &= mdl.load_model("rock_cluster", models_path + "rock_cluster.glb");
    success &= mdl.load_model("rock_mossy", models_path + "rock_mossy.glb");
    success &= mdl.load_model("tree_oak", models_path + "tree_oak.glb");
    success &= mdl.load_model("tree_pine", models_path + "tree_pine.glb");
    success &= mdl.load_model("tree_dead", models_path + "tree_dead.glb");

    // Weapons/effects
    success &= mdl.load_model("weapon_sword", models_path + "weapon_sword.glb");
    success &= mdl.load_model("spell_fireball", models_path + "spell_fireball.glb");
    success &= mdl.load_model("spell_bible", models_path + "spell_bible.glb");

    // Terrain
    mdl.load_model("ground_grass", models_path + "ground_grass.glb");
    mdl.load_model("ground_stone", models_path + "ground_stone.glb");
    mdl.load_model("mountain_small", models_path + "mountain_small.glb");
    mdl.load_model("mountain_medium", models_path + "mountain_medium.glb");
    mdl.load_model("mountain_large", models_path + "mountain_large.glb");

    if (success) {
        std::cout << "All 3D models loaded successfully" << std::endl;
    } else {
        std::cerr << "Warning: Some models failed to load" << std::endl;
    }

    return success;
}

} // namespace mmo::editor
