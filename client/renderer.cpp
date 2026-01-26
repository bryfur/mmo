#include "renderer.hpp"
#include "render/grass_renderer.hpp"
#include "common/entity_config.hpp"
#include "shader.hpp"
#include <GL/glew.h>
#include <glm/gtc/type_ptr.hpp>
#include <SDL3/SDL_log.h>
#include <iostream>
#include <cmath>
#include <type_traits>

namespace mmo {

Renderer::Renderer() 
    : model_manager_(std::make_unique<ModelManager>()),
      grass_renderer_(std::make_unique<GrassRenderer>()) {}

Renderer::~Renderer() {
    shutdown();
}

bool Renderer::init(int width, int height, const std::string& title) {
    // Initialize render context (SDL window + GPU device)
    if (!context_.init(width, height, title)) {
        return false;
    }
    
    // Initialize pipeline registry for SDL3 GPU API
    if (!pipeline_registry_.init(context_.device())) {
        std::cerr << "Failed to initialize pipeline registry" << std::endl;
        return false;
    }
    pipeline_registry_.set_swapchain_format(context_.swapchain_format());
    
    // Initialize terrain renderer with GPU device and pipeline registry
    if (!terrain_.init(context_.device(), pipeline_registry_, WORLD_WIDTH, WORLD_HEIGHT)) {
        std::cerr << "Failed to initialize terrain renderer" << std::endl;
        return false;
    }
    
    // Initialize world renderer (skybox, mountains, rocks, trees, grid)
    if (!world_.init(WORLD_WIDTH, WORLD_HEIGHT, model_manager_.get())) {
        std::cerr << "Failed to initialize world renderer" << std::endl;
        return false;
    }
    
    // Set terrain height callback for world objects
    world_.set_terrain_height_func([this](float x, float z) {
        return terrain_.get_height(x, z);
    });
    
    // Initialize UI renderer
    if (!ui_.init(width, height)) {
        std::cerr << "Failed to initialize UI renderer" << std::endl;
        return false;
    }
    
    // Initialize effect renderer
    if (!effects_.init(model_manager_.get())) {
        std::cerr << "Failed to initialize effect renderer" << std::endl;
        return false;
    }
    effects_.set_terrain_height_func([this](float x, float z) {
        return terrain_.get_height(x, z);
    });
    
    // Initialize shadow system
    if (!shadows_.init(4096)) {
        std::cerr << "Failed to initialize shadow system" << std::endl;
        return false;
    }
    
    // Initialize SSAO system
    if (!ssao_.init(width, height)) {
        std::cerr << "Failed to initialize SSAO system" << std::endl;
        return false;
    }
    
    // Initialize GPU resources for entity rendering
    init_pipelines();
    init_billboard_buffers();
    
    // Initialize grass renderer
    if (grass_renderer_) {
        grass_renderer_->init(WORLD_WIDTH, WORLD_HEIGHT);
    }
    
    return true;
}

void Renderer::init_pipelines() {
    // Preload commonly used pipelines to avoid hitching during gameplay
    pipeline_registry_.get_model_pipeline();
    pipeline_registry_.get_skinned_model_pipeline();
    pipeline_registry_.get_billboard_pipeline();
    
    // Create default sampler for model textures
    SDL_GPUSamplerCreateInfo sampler_info = {};
    sampler_info.min_filter = SDL_GPU_FILTER_LINEAR;
    sampler_info.mag_filter = SDL_GPU_FILTER_LINEAR;
    sampler_info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
    sampler_info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    sampler_info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    sampler_info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    sampler_info.max_anisotropy = 16.0f;
    sampler_info.enable_anisotropy = true;
    default_sampler_ = context_.device().create_sampler(sampler_info);
}

void Renderer::init_billboard_buffers() {
    // Create a dynamic vertex buffer for billboard rendering (health bars, etc.)
    // 6 vertices per quad * 7 floats per vertex (pos3 + color4)
    constexpr size_t BILLBOARD_BUFFER_SIZE = 6 * 7 * sizeof(float);
    billboard_vertex_buffer_ = gpu::GPUBuffer::create_dynamic(
        context_.device(), 
        gpu::GPUBuffer::Type::Vertex, 
        BILLBOARD_BUFFER_SIZE
    );
}

void Renderer::shutdown() {
    if (model_manager_) {
        model_manager_->unload_all();
    }
    
    if (grass_renderer_) {
        grass_renderer_->shutdown();
    }
    
    // Release GPU resources
    billboard_vertex_buffer_.reset();
    
    if (default_sampler_) {
        context_.device().release_sampler(default_sampler_);
        default_sampler_ = nullptr;
    }
    
    // Shutdown pipeline registry
    pipeline_registry_.shutdown();
    
    // Shutdown subsystems
    effects_.shutdown();
    ui_.shutdown();
    world_.shutdown();
    terrain_.shutdown();
    ssao_.shutdown();
    shadows_.shutdown();
    context_.shutdown();
}

bool Renderer::load_models(const std::string& assets_path) {
    std::string models_path = assets_path + "/models/";
    
    bool success = true;
    
    // Player models
    if (!model_manager_->load_model("warrior", models_path + "warrior_rigged.glb")) {
        success &= model_manager_->load_model("warrior", models_path + "warrior.glb");
    }
    if (!model_manager_->load_model("mage", models_path + "mage_rigged.glb")) {
        success &= model_manager_->load_model("mage", models_path + "mage.glb");
    }
    if (!model_manager_->load_model("paladin", models_path + "paladin_rigged.glb")) {
        success &= model_manager_->load_model("paladin", models_path + "paladin.glb");
    }
    if (!model_manager_->load_model("archer", models_path + "archer_rigged.glb")) {
        success &= model_manager_->load_model("archer", models_path + "archer.glb");
    }
    success &= model_manager_->load_model("npc", models_path + "npc_enemy.glb");
    
    // Ground tiles
    model_manager_->load_model("ground_grass", models_path + "ground_grass.glb");
    model_manager_->load_model("ground_stone", models_path + "ground_stone.glb");
    
    // Mountain models
    model_manager_->load_model("mountain_small", models_path + "mountain_small.glb");
    model_manager_->load_model("mountain_medium", models_path + "mountain_medium.glb");
    model_manager_->load_model("mountain_large", models_path + "mountain_large.glb");
    
    // Buildings
    model_manager_->load_model("building_tavern", models_path + "building_tavern.glb");
    model_manager_->load_model("building_blacksmith", models_path + "building_blacksmith.glb");
    model_manager_->load_model("building_tower", models_path + "building_tower.glb");
    model_manager_->load_model("building_shop", models_path + "building_shop.glb");
    model_manager_->load_model("building_well", models_path + "building_well.glb");
    model_manager_->load_model("building_house", models_path + "building_house.glb");
    model_manager_->load_model("building_inn", models_path + "inn.glb");
    model_manager_->load_model("wooden_log", models_path + "wooden_log.glb");
    model_manager_->load_model("log_tower", models_path + "log_tower.glb");
    
    // Town NPCs
    model_manager_->load_model("npc_merchant", models_path + "npc_merchant.glb");
    model_manager_->load_model("npc_guard", models_path + "npc_guard.glb");
    model_manager_->load_model("npc_blacksmith", models_path + "npc_blacksmith.glb");
    model_manager_->load_model("npc_innkeeper", models_path + "npc_innkeeper.glb");
    model_manager_->load_model("npc_villager", models_path + "npc_villager.glb");
    
    // Attack effect models
    model_manager_->load_model("weapon_sword", models_path + "weapon_sword.glb");
    model_manager_->load_model("spell_fireball", models_path + "spell_fireball.glb");
    model_manager_->load_model("spell_bible", models_path + "spell_bible.glb");
    
    // Rock models
    model_manager_->load_model("rock_boulder", models_path + "rock_boulder.glb");
    model_manager_->load_model("rock_slate", models_path + "rock_slate.glb");
    model_manager_->load_model("rock_spire", models_path + "rock_spire.glb");
    model_manager_->load_model("rock_cluster", models_path + "rock_cluster.glb");
    model_manager_->load_model("rock_mossy", models_path + "rock_mossy.glb");
    
    // Tree models
    model_manager_->load_model("tree_oak", models_path + "tree_oak.glb");
    model_manager_->load_model("tree_pine", models_path + "tree_pine.glb");
    model_manager_->load_model("tree_dead", models_path + "tree_dead.glb");
    
    if (success) {
        models_loaded_ = true;
        std::cout << "All 3D models loaded successfully" << std::endl;
    } else {
        std::cerr << "Warning: Some models failed to load" << std::endl;
    }
    
    return success;
}

// ============================================================================
// FRAME MANAGEMENT
// ============================================================================

void Renderer::begin_frame() {
    context_.begin_frame();
    
    // Update camera system screen size
    camera_system_.set_screen_size(context_.width(), context_.height());
    ui_.set_screen_size(context_.width(), context_.height());
}

void Renderer::end_frame() {
    context_.end_frame();
}

// ============================================================================
// SHADOW PASS
// ============================================================================

void Renderer::begin_shadow_pass() {
    if (!shadows_.is_enabled()) return;
    
    // Update light space matrix based on camera position
    shadows_.update_light_space_matrix(camera_x_, camera_y_, light_dir_);
    shadows_.begin_shadow_pass();
}

void Renderer::end_shadow_pass() {
    shadows_.end_shadow_pass();
}

void Renderer::draw_entity_shadow(const EntityState& entity) {
    if (!shadows_.is_enabled()) return;
    
    Model* model = get_model_for_entity(entity);
    if (!model) return;
    
    // Use server-provided height (entity.z) instead of recalculating terrain height
    glm::vec3 position(entity.x, entity.z, entity.y);
    
    float rotation = 0.0f;
    if (entity.type == EntityType::Building || entity.type == EntityType::Environment) {
        rotation = entity.rotation;
    } else if (entity.attack_dir_x != 0.0f || entity.attack_dir_y != 0.0f) {
        rotation = std::atan2(-entity.attack_dir_x, -entity.attack_dir_y);
    } else if (entity.vx != 0.0f || entity.vy != 0.0f) {
        rotation = std::atan2(-entity.vx, -entity.vy);
    }
    
    float scale = 50.0f;
    if (entity.type == EntityType::Building) {
        scale = 250.0f;
    } else if (entity.type == EntityType::Environment) {
        // Environment objects use their scale directly
        float model_size = model->max_dimension();
        scale = (entity.scale * 1.5f) / model_size;
        scale *= model_size;  // Convert back to final scale for shadow
    }
    
    draw_model_shadow(model, position, rotation, scale);
}

void Renderer::draw_model_shadow(Model* model, const glm::vec3& position, float rotation, float scale) {
    if (!model || !shadows_.is_enabled()) return;
    
    Shader* shader = shadows_.shadow_shader();
    if (!shader) return;
    
    shader->use();
    shader->set_mat4("lightSpaceMatrix", shadows_.light_space_matrix());
    
    glm::mat4 model_mat = glm::mat4(1.0f);
    model_mat = glm::translate(model_mat, position);
    model_mat = glm::rotate(model_mat, rotation, glm::vec3(0.0f, 1.0f, 0.0f));
    model_mat = glm::scale(model_mat, glm::vec3(scale));
    
    float cx = (model->min_x + model->max_x) / 2.0f;
    float cy = model->min_y;
    float cz = (model->min_z + model->max_z) / 2.0f;
    model_mat = model_mat * glm::translate(glm::mat4(1.0f), glm::vec3(-cx, -cy, -cz));
    
    shader->set_mat4("model", model_mat);
    
    // Note: Model meshes still use GL buffers until issue #14 is completed
    // For now, continue using the GL draw path
    for (const auto& mesh : model->meshes) {
        if (mesh.vao && !mesh.indices.empty()) {
            glBindVertexArray(mesh.vao);
            glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mesh.indices.size()), GL_UNSIGNED_INT, 0);
        }
    }
    glBindVertexArray(0);
}

void Renderer::draw_mountain_shadows() {
    if (!shadows_.is_enabled()) return;
    
    Model* mountain_large = model_manager_->get_model("mountain_large");
    if (!mountain_large) return;
    
    Shader* shader = shadows_.shadow_shader();
    if (!shader) return;
    
    shader->use();
    shader->set_mat4("lightSpaceMatrix", shadows_.light_space_matrix());
    
    for (const auto& mp : world_.get_mountain_positions()) {
        if (mp.size_type != 2) continue;  // Only large mountains
        
        float dx = mp.x - camera_x_;
        float dz = mp.z - camera_y_;
        float dist = std::sqrt(dx * dx + dz * dz);
        float light_dot = (dx * (-light_dir_.x) + dz * (-light_dir_.z));
        
        if (light_dot > 0 && dist < 15000.0f) {
            glm::vec3 pos(mp.x, mp.y, mp.z);
            draw_model_shadow(mountain_large, pos, glm::radians(mp.rotation), mp.scale);
        }
    }
}

void Renderer::draw_tree_shadows() {
    // Trees are now rendered as server-side entities with collision
    // Their shadows are rendered through draw_entity_shadow
}

// ============================================================================
// CAMERA
// ============================================================================

void Renderer::set_camera(float x, float y) {
    camera_x_ = x;
    camera_y_ = y;
}

void Renderer::set_camera_velocity(float vx, float vy) {
    camera_system_.set_target_velocity(glm::vec3(vx, 0.0f, vy));
}

void Renderer::set_camera_orbit(float yaw, float pitch) {
    camera_system_.set_yaw(yaw);
    camera_system_.set_pitch(pitch);
}

void Renderer::adjust_camera_zoom(float delta) {
    camera_system_.adjust_zoom(delta);
}

void Renderer::update_camera() {
    camera_system_.set_screen_size(context_.width(), context_.height());
    camera_system_.set_terrain_height_func([this](float x, float z) {
        return terrain_.get_height(x, z);
    });
    
    float terrain_y = terrain_.get_height(camera_x_, camera_y_);
    camera_system_.set_target(glm::vec3(camera_x_, terrain_y, camera_y_));
    camera_system_.update(0.016f);
    
    view_ = camera_system_.get_view_matrix();
    projection_ = camera_system_.get_projection_matrix();
    actual_camera_pos_ = camera_system_.get_position();
}

void Renderer::update_camera_smooth(float dt) {
    camera_system_.set_screen_size(context_.width(), context_.height());
    camera_system_.set_terrain_height_func([this](float x, float z) {
        return terrain_.get_height(x, z);
    });
    
    float terrain_y = terrain_.get_height(camera_x_, camera_y_);
    camera_system_.set_target(glm::vec3(camera_x_, terrain_y, camera_y_));
    camera_system_.update(dt);
    
    view_ = camera_system_.get_view_matrix();
    projection_ = camera_system_.get_projection_matrix();
    actual_camera_pos_ = camera_system_.get_position();
}

void Renderer::notify_player_attack() {
    camera_system_.notify_attack();
}

void Renderer::notify_player_hit(float dir_x, float dir_y, float damage) {
    camera_system_.notify_hit(glm::vec3(dir_x, 0.0f, dir_y), damage);
}

void Renderer::set_in_combat(bool in_combat) {
    camera_system_.set_in_combat(in_combat);
}

void Renderer::set_sprinting(bool sprinting) {
    if (sprinting) {
        camera_system_.set_mode(CameraMode::Sprint);
    }
}

// ============================================================================
// GRAPHICS SETTINGS
// ============================================================================

void Renderer::set_shadows_enabled(bool enabled) {
    shadows_.set_enabled(enabled);
}

void Renderer::set_ssao_enabled(bool enabled) {
    ssao_.set_enabled(enabled);
}

void Renderer::set_fog_enabled(bool enabled) {
    fog_enabled_ = enabled;
}

void Renderer::set_grass_enabled(bool enabled) {
    grass_enabled_ = enabled;
}

void Renderer::set_anisotropic_filter(int level) {
    anisotropic_level_ = level;
    
    // Convert level to actual anisotropy value: 0=1 (off), 1=2, 2=4, 3=8, 4=16
    float aniso_value = 1.0f;
    if (level > 0) {
        aniso_value = static_cast<float>(1 << level);  // 2, 4, 8, 16
    }
    
    // Cap at 16x (typical hardware max)
    aniso_value = std::min(aniso_value, 16.0f);
    
    // Update all model textures
    if (model_manager_) {
        model_manager_->set_anisotropic_filter(aniso_value);
    }
    
    // Update terrain renderer's anisotropic filtering
    terrain_.set_anisotropic_filter(aniso_value);
    
    // Recreate default sampler with new anisotropy settings
    if (default_sampler_) {
        context_.device().release_sampler(default_sampler_);
        
        SDL_GPUSamplerCreateInfo sampler_info = {};
        sampler_info.min_filter = SDL_GPU_FILTER_LINEAR;
        sampler_info.mag_filter = SDL_GPU_FILTER_LINEAR;
        sampler_info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
        sampler_info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
        sampler_info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
        sampler_info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
        sampler_info.max_anisotropy = aniso_value;
        sampler_info.enable_anisotropy = (level > 0);
        default_sampler_ = context_.device().create_sampler(sampler_info);
    }
}

bool Renderer::get_shadows_enabled() const {
    return shadows_.is_enabled();
}

bool Renderer::get_ssao_enabled() const {
    return ssao_.is_enabled();
}

void Renderer::set_heightmap(const HeightmapChunk& heightmap) {
    // Pass heightmap to terrain renderer for GPU upload
    terrain_.set_heightmap(heightmap);
    
    // Note: GrassRenderer still uses OpenGL and cannot use the SDL3 GPU heightmap texture
    // The grass renderer integration will be updated in a future task
    // TODO: Update grass_renderer to use SDL3 GPU API (Issue #15)
    
    std::cout << "[Renderer] Heightmap set for terrain rendering" << std::endl;
}

// ============================================================================
// WORLD RENDERING (delegates to subsystems)
// ============================================================================

void Renderer::draw_skybox() {
    if (!skybox_enabled_) return;
    skybox_time_ += 0.016f;
    world_.update(0.016f);
    world_.render_skybox(view_, projection_);
}

void Renderer::draw_distant_mountains() {
    if (!mountains_enabled_) return;
    world_.render_mountains(view_, projection_, actual_camera_pos_, light_dir_);
}

void Renderer::draw_rocks() {
    // Rocks are now rendered as server-side entities with collision
    // The old client-side procedural rocks have been replaced
    // This function is kept for compatibility but does nothing
}

void Renderer::draw_trees() {
    // Trees are now rendered as server-side entities with collision
    // The old client-side procedural trees have been replaced
    // This function is kept for compatibility but does nothing
}

void Renderer::draw_ground() {
    // TODO: Terrain rendering using SDL3 GPU API
    // The terrain renderer has been ported to SDL3 GPU API (using GPUBuffer, GPUTexture, etc.)
    // However, it requires a render pass and command buffer to be set up by the main renderer.
    // This will be integrated when the Main Renderer Class (issue #13) is updated.
    //
    // For now, terrain rendering is temporarily disabled.
    // The terrain height queries (get_height, get_normal) still work for physics/placement.
    //
    // To re-enable, the render() call should be:
    // terrain_.render(pass, cmd, view_, projection_, actual_camera_pos_,
    //                 light_space_matrix, shadow_map, shadow_sampler,
    //                 shadows_enabled, ssao_texture, ssao_sampler,
    //                 ssao_enabled, light_dir_, screen_size);
}

void Renderer::draw_grass() {
    if (!grass_renderer_ || !grass_enabled_) return;
    
    grass_renderer_->update(0.016f, skybox_time_);
    grass_renderer_->render(view_, projection_, actual_camera_pos_,
                           shadows_.light_space_matrix(), shadows_.shadow_depth_texture(),
                           shadows_.is_enabled(), light_dir_);
}

void Renderer::draw_grid() {
    world_.render_grid(view_, projection_);
}

// ============================================================================
// ENTITY RENDERING
// ============================================================================

Model* Renderer::get_model_for_entity(const EntityState& entity) {
    if (entity.type == EntityType::NPC) {
        return model_manager_->get_model("npc");
    }
    
    if (entity.type == EntityType::TownNPC) {
        switch (entity.npc_type) {
            case NPCType::Merchant:   return model_manager_->get_model("npc_merchant");
            case NPCType::Guard:      return model_manager_->get_model("npc_guard");
            case NPCType::Blacksmith: return model_manager_->get_model("npc_blacksmith");
            case NPCType::Innkeeper:  return model_manager_->get_model("npc_innkeeper");
            case NPCType::Villager:   return model_manager_->get_model("npc_villager");
            default:                  return model_manager_->get_model("npc_villager");
        }
    }
    
    if (entity.type == EntityType::Building) {
        switch (entity.building_type) {
            case BuildingType::Tavern:      return model_manager_->get_model("building_tavern");
            case BuildingType::Blacksmith:  return model_manager_->get_model("building_blacksmith");
            case BuildingType::Tower:       return model_manager_->get_model("building_tower");
            case BuildingType::Shop:        return model_manager_->get_model("building_shop");
            case BuildingType::Well:        return model_manager_->get_model("building_well");
            case BuildingType::House:       return model_manager_->get_model("building_house");
            case BuildingType::Inn:         return model_manager_->get_model("building_inn");
            case BuildingType::WoodenLog:   return model_manager_->get_model("wooden_log");
            case BuildingType::LogTower:    return model_manager_->get_model("log_tower");
            default:                        return model_manager_->get_model("building_house");
        }
    }
    
    if (entity.type == EntityType::Environment) {
        switch (entity.environment_type) {
            case EnvironmentType::RockBoulder:  return model_manager_->get_model("rock_boulder");
            case EnvironmentType::RockSlate:    return model_manager_->get_model("rock_slate");
            case EnvironmentType::RockSpire:    return model_manager_->get_model("rock_spire");
            case EnvironmentType::RockCluster:  return model_manager_->get_model("rock_cluster");
            case EnvironmentType::RockMossy:    return model_manager_->get_model("rock_mossy");
            case EnvironmentType::TreeOak:      return model_manager_->get_model("tree_oak");
            case EnvironmentType::TreePine:     return model_manager_->get_model("tree_pine");
            case EnvironmentType::TreeDead:     return model_manager_->get_model("tree_dead");
            default:                            return model_manager_->get_model("rock_boulder");
        }
    }
    
    switch (entity.player_class) {
        case PlayerClass::Warrior: return model_manager_->get_model("warrior");
        case PlayerClass::Mage:    return model_manager_->get_model("mage");
        case PlayerClass::Paladin: return model_manager_->get_model("paladin");
        case PlayerClass::Archer:  return model_manager_->get_model("archer");
        default:                   return model_manager_->get_model("warrior");
    }
}

void Renderer::draw_entity(const EntityState& entity, bool is_local) {
    Model* model = get_model_for_entity(entity);
    if (!model || !models_loaded_) return;
    
    float rotation = 0.0f;
    if (entity.type == EntityType::Building || entity.type == EntityType::Environment) {
        rotation = entity.rotation;  // Use pre-set rotation for buildings and environment
    } else if (entity.type == EntityType::Player) {
        rotation = std::atan2(entity.attack_dir_x, entity.attack_dir_y);
    } else if (entity.vx != 0.0f || entity.vy != 0.0f) {
        rotation = std::atan2(entity.vx, entity.vy);
    }
    
    float target_size;
    bool show_health_bar = true;
    
    switch (entity.type) {
        case EntityType::Building:
            target_size = config::get_building_target_size(entity.building_type);
            show_health_bar = false;
            break;
        case EntityType::Environment:
            // Environment objects use scale directly (already calculated on server)
            target_size = entity.scale;
            show_health_bar = false;
            break;
        case EntityType::TownNPC:
            target_size = config::get_character_target_size(EntityType::TownNPC);
            show_health_bar = false;
            break;
        case EntityType::NPC:
            target_size = config::get_character_target_size(EntityType::NPC);
            break;
        default:
            target_size = config::get_character_target_size(EntityType::Player);
            break;
    }
    
    // Apply per-instance scale from server (but not for Environment, already applied)
    if (entity.type != EntityType::Environment) {
        target_size *= entity.scale;
    }
    
    float model_size = model->max_dimension();
    float scale = (target_size * 1.5f) / model_size;
    
    // Use server-provided height (entity.z) for accurate terrain placement
    glm::vec3 position(entity.x, entity.z, entity.y);
    glm::vec4 tint(1.0f);
    
    float attack_tilt = 0.0f;
    if (entity.is_attacking && entity.attack_cooldown > 0.0f) {
        float max_cooldown = 0.5f;
        float progress = std::min(entity.attack_cooldown / max_cooldown, 1.0f);
        attack_tilt = std::sin(progress * 3.14159f) * 0.4f;
    }
    
    if (model->has_skeleton && entity.type == EntityType::Player) {
        std::string model_name;
        switch (entity.player_class) {
            case PlayerClass::Warrior: model_name = "warrior"; break;
            case PlayerClass::Mage:    model_name = "mage"; break;
            case PlayerClass::Paladin: model_name = "paladin"; break;
            case PlayerClass::Archer:  model_name = "archer"; break;
        }
        
        std::string anim_name;
        if (entity.is_attacking) {
            anim_name = "Attack";
        } else if (std::abs(entity.vx) > 1.0f || std::abs(entity.vy) > 1.0f) {
            anim_name = "Walk";
        } else {
            anim_name = "Idle";
        }
        set_entity_animation(model_name, anim_name);
    }
    
    draw_model(model, position, rotation, scale, tint, attack_tilt);
    
    if (show_health_bar && !is_local) {
        float health_ratio = entity.health / entity.max_health;
        float bar_height_offset = entity.z + target_size * 1.3f;
        draw_enemy_health_bar_3d(entity.x, bar_height_offset, entity.y, target_size * 0.8f, health_ratio);
    }
}

void Renderer::draw_player(const PlayerState& player, bool is_local) {
    draw_entity(player, is_local);
}

void Renderer::draw_model(Model* model, const glm::vec3& position, float rotation, float scale, 
                          const glm::vec4& tint, float attack_tilt) {
    if (!model) return;
    
    // Get appropriate pipeline from registry
    // Note: Until issue #14 (Model Loader migration) is complete, we use legacy GL shaders
    // This is a temporary compatibility layer during the phased SDL3 GPU migration
    auto* pipeline = model->has_skeleton ? 
                     pipeline_registry_.get_skinned_model_pipeline() :
                     pipeline_registry_.get_model_pipeline();
    
    // For now, fall back to the legacy GL shader path since models still use GL buffers
    // TODO: Once issue #14 is complete, use SDL3 GPU rendering path
    Shader legacy_model_shader;
    Shader legacy_skinned_shader;
    
    // Use legacy embedded shaders for compatibility
    if (model->has_skeleton) {
        legacy_skinned_shader.load(shaders::skinned_model_vertex, shaders::skinned_model_fragment);
        legacy_skinned_shader.use();
    } else {
        legacy_model_shader.load(shaders::model_vertex, shaders::model_fragment);
        legacy_model_shader.use();
    }
    
    Shader* shader = model->has_skeleton ? &legacy_skinned_shader : &legacy_model_shader;
    
    glm::mat4 model_mat = glm::mat4(1.0f);
    model_mat = glm::translate(model_mat, position);
    model_mat = glm::rotate(model_mat, rotation, glm::vec3(0.0f, 1.0f, 0.0f));
    if (attack_tilt != 0.0f) {
        model_mat = glm::rotate(model_mat, attack_tilt, glm::vec3(1.0f, 0.0f, 0.0f));
    }
    model_mat = glm::scale(model_mat, glm::vec3(scale));
    
    float cx = (model->min_x + model->max_x) / 2.0f;
    float cy = model->min_y;
    float cz = (model->min_z + model->max_z) / 2.0f;
    model_mat = model_mat * glm::translate(glm::mat4(1.0f), glm::vec3(-cx, -cy, -cz));
    
    shader->set_mat4("model", model_mat);
    shader->set_mat4("view", view_);
    shader->set_mat4("projection", projection_);
    shader->set_vec3("cameraPos", actual_camera_pos_);
    shader->set_vec3("fogColor", glm::vec3(0.35f, 0.45f, 0.6f));
    shader->set_float("fogStart", 800.0f);
    shader->set_float("fogEnd", 4000.0f);
    shader->set_int("fogEnabled", fog_enabled_ ? 1 : 0);
    shader->set_vec3("lightDir", light_dir_);
    shader->set_vec3("lightColor", glm::vec3(1.0f, 0.95f, 0.9f));
    shader->set_vec3("ambientColor", glm::vec3(0.4f, 0.4f, 0.5f));
    shader->set_vec4("tintColor", tint);
    
    shader->set_mat4("lightSpaceMatrix", shadows_.light_space_matrix());
    shader->set_int("shadowsEnabled", shadows_.is_enabled() ? 1 : 0);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, shadows_.shadow_depth_texture());
    shader->set_int("shadowMap", 2);
    
    shader->set_int("ssaoEnabled", ssao_.is_enabled() ? 1 : 0);
    shader->set_vec2("screenSize", glm::vec2(context_.width(), context_.height()));
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, ssao_.ssao_texture());
    shader->set_int("ssaoTexture", 3);
    
    if (model->has_skeleton) {
        AnimationState* anim_state = nullptr;
        static const char* animated_models[] = {"warrior", "mage", "paladin"};
        for (const char* name : animated_models) {
            if (model_manager_->get_model(name) == model) {
                anim_state = model_manager_->get_animation_state(name);
                break;
            }
        }
        
        if (anim_state) {
            shader->set_int("useSkinning", 1);
            GLint loc = glGetUniformLocation(shader->id(), "boneMatrices");
            if (loc >= 0) {
                glUniformMatrix4fv(loc, MAX_BONES, GL_FALSE, 
                                   glm::value_ptr(anim_state->bone_matrices[0]));
            }
        } else {
            shader->set_int("useSkinning", 0);
        }
    }
    
    for (auto& mesh : model->meshes) {
        if (!mesh.uploaded) {
            ModelLoader::upload_to_gpu(*model);
        }
        
        if (mesh.vao && !mesh.indices.empty()) {
            if (mesh.has_texture && mesh.texture_id > 0) {
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, mesh.texture_id);
                shader->set_int("baseColorTexture", 0);
                shader->set_int("hasTexture", 1);
            } else {
                shader->set_int("hasTexture", 0);
            }
            
            glBindVertexArray(mesh.vao);
            glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mesh.indices.size()), GL_UNSIGNED_INT, 0);
            glBindVertexArray(0);
        }
    }
}

void Renderer::draw_model_no_fog(Model* model, const glm::vec3& position, float rotation, 
                                  float scale, const glm::vec4& tint) {
    if (!model) return;
    
    // Use legacy GL shader path for compatibility
    Shader legacy_shader;
    legacy_shader.load(shaders::model_vertex, shaders::model_fragment);
    legacy_shader.use();
    
    glm::mat4 model_mat = glm::mat4(1.0f);
    model_mat = glm::translate(model_mat, position);
    model_mat = glm::rotate(model_mat, rotation, glm::vec3(0.0f, 1.0f, 0.0f));
    model_mat = glm::scale(model_mat, glm::vec3(scale));
    
    float cx = (model->min_x + model->max_x) / 2.0f;
    float cy = model->min_y;
    float cz = (model->min_z + model->max_z) / 2.0f;
    model_mat = model_mat * glm::translate(glm::mat4(1.0f), glm::vec3(-cx, -cy, -cz));
    
    legacy_shader.set_mat4("model", model_mat);
    legacy_shader.set_mat4("view", view_);
    legacy_shader.set_mat4("projection", projection_);
    legacy_shader.set_vec3("cameraPos", actual_camera_pos_);
    legacy_shader.set_vec3("fogColor", glm::vec3(0.55f, 0.55f, 0.6f));
    legacy_shader.set_float("fogStart", 3000.0f);
    legacy_shader.set_float("fogEnd", 12000.0f);
    legacy_shader.set_int("fogEnabled", 1);
    legacy_shader.set_vec3("lightDir", light_dir_);
    legacy_shader.set_vec3("lightColor", glm::vec3(1.0f, 0.95f, 0.9f));
    legacy_shader.set_vec3("ambientColor", glm::vec3(0.5f, 0.5f, 0.55f));
    legacy_shader.set_vec4("tintColor", tint);
    legacy_shader.set_int("shadowsEnabled", 0);
    legacy_shader.set_int("ssaoEnabled", 0);
    
    for (auto& mesh : model->meshes) {
        if (!mesh.uploaded) {
            ModelLoader::upload_to_gpu(*model);
        }
        
        if (mesh.vao && !mesh.indices.empty()) {
            if (mesh.has_texture && mesh.texture_id > 0) {
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, mesh.texture_id);
                legacy_shader.set_int("baseColorTexture", 0);
                legacy_shader.set_int("hasTexture", 1);
            } else {
                legacy_shader.set_int("hasTexture", 0);
            }
            
            glBindVertexArray(mesh.vao);
            glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mesh.indices.size()), GL_UNSIGNED_INT, 0);
            glBindVertexArray(0);
        }
    }
}

void Renderer::update_animations(float dt) {
    static const char* animated_models[] = {"warrior", "mage", "paladin"};
    for (const char* name : animated_models) {
        Model* model = model_manager_->get_model(name);
        AnimationState* state = model_manager_->get_animation_state(name);
        if (model && state) {
            ModelLoader::update_animation(*model, *state, dt);
        }
    }
}

void Renderer::set_entity_animation(const std::string& model_name, const std::string& anim_name) {
    Model* model = model_manager_->get_model(model_name);
    AnimationState* state = model_manager_->get_animation_state(model_name);
    if (!model || !state) return;
    
    int clip_idx = model->find_animation(anim_name);
    if (clip_idx >= 0 && clip_idx != state->current_clip) {
        state->current_clip = clip_idx;
        state->time = 0.0f;
        state->playing = true;
    }
}

// ============================================================================
// HEALTH BARS
// ============================================================================

void Renderer::draw_player_health_ui(float health_ratio, float max_health) {
    ui_.draw_player_health_bar(health_ratio, max_health, context_.width(), context_.height());
}

void Renderer::draw_enemy_health_bar_3d(float world_x, float world_y, float world_z, 
                                         float bar_width, float health_ratio) {
    glm::vec4 world_pos(world_x, world_y, world_z, 1.0f);
    glm::vec4 clip_pos = projection_ * view_ * world_pos;
    if (clip_pos.w <= 0.01f) return;
    
    glm::vec3 ndc = glm::vec3(clip_pos) / clip_pos.w;
    if (ndc.x < -1.5f || ndc.x > 1.5f || ndc.y < -1.5f || ndc.y > 1.5f || ndc.z < -1.0f || ndc.z > 1.0f) {
        return;
    }
    
    // Use legacy GL billboard shader for compatibility
    // TODO: Migrate to SDL3 GPU billboard pipeline once issue #14 completes
    Shader billboard_shader;
    billboard_shader.load(shaders::billboard_vertex, shaders::billboard_fragment);
    billboard_shader.use();
    billboard_shader.set_mat4("view", view_);
    billboard_shader.set_mat4("projection", projection_);
    billboard_shader.set_vec3("worldPos", glm::vec3(world_x, world_y, world_z));
    
    float world_bar_width = bar_width * 0.5f;
    float world_bar_height = bar_width * 0.1f;
    
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_CULL_FACE);
    glDepthMask(GL_FALSE);
    
    // Create temporary VAO/VBO for billboard rendering
    GLuint temp_vao, temp_vbo;
    glGenVertexArrays(1, &temp_vao);
    glGenBuffers(1, &temp_vbo);
    
    glBindVertexArray(temp_vao);
    glBindBuffer(GL_ARRAY_BUFFER, temp_vbo);
    glBufferData(GL_ARRAY_BUFFER, 6 * 7 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    
    auto draw_billboard_quad = [&](float offset_x, float offset_y, float w, float h, const glm::vec4& color) {
        billboard_shader.set_vec2("size", glm::vec2(w, h));
        billboard_shader.set_vec2("offset", glm::vec2(offset_x, offset_y));
        
        float vertices[] = {
            -0.5f, -0.5f, 0.0f, color.r, color.g, color.b, color.a,
             0.5f, -0.5f, 0.0f, color.r, color.g, color.b, color.a,
             0.5f,  0.5f, 0.0f, color.r, color.g, color.b, color.a,
            -0.5f, -0.5f, 0.0f, color.r, color.g, color.b, color.a,
             0.5f,  0.5f, 0.0f, color.r, color.g, color.b, color.a,
            -0.5f,  0.5f, 0.0f, color.r, color.g, color.b, color.a,
        };
        
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
        glDrawArrays(GL_TRIANGLES, 0, 6);
    };
    
    glm::vec4 bg_color(0.0f, 0.0f, 0.0f, 0.8f);
    glm::vec4 empty_color(0.4f, 0.0f, 0.0f, 0.9f);
    glm::vec4 health_color(0.0f, 0.8f, 0.0f, 1.0f);
    
    draw_billboard_quad(0.0f, 0.0f, world_bar_width + 2.0f, world_bar_height + 2.0f, bg_color);
    draw_billboard_quad(0.0f, 0.0f, world_bar_width, world_bar_height, empty_color);
    
    float fill_width = world_bar_width * health_ratio;
    float fill_offset_x = (fill_width - world_bar_width) * 0.5f;
    draw_billboard_quad(fill_offset_x, 0.0f, fill_width, world_bar_height, health_color);
    
    glBindVertexArray(0);
    
    // Clean up temporary resources
    glDeleteVertexArrays(1, &temp_vao);
    glDeleteBuffers(1, &temp_vbo);
    
    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);
}

// ============================================================================
// ATTACK EFFECTS (delegates to EffectRenderer)
// ============================================================================

void Renderer::draw_attack_effect(const ecs::AttackEffect& effect) {
    effects_.draw_attack_effect(effect, view_, projection_);
}

void Renderer::draw_warrior_slash(float x, float y, float dir_x, float dir_y, float progress) {
    ecs::AttackEffect effect;
    effect.attacker_class = PlayerClass::Warrior;
    effect.x = x;
    effect.y = y;
    effect.direction_x = dir_x;
    effect.direction_y = dir_y;
    effect.duration = 0.3f;
    effect.timer = effect.duration * (1.0f - progress);
    effects_.draw_attack_effect(effect, view_, projection_);
}

void Renderer::draw_mage_beam(float x, float y, float dir_x, float dir_y, float progress, float range) {
    ecs::AttackEffect effect;
    effect.attacker_class = PlayerClass::Mage;
    effect.x = x;
    effect.y = y;
    effect.direction_x = dir_x;
    effect.direction_y = dir_y;
    effect.duration = 0.4f;
    effect.timer = effect.duration * (1.0f - progress);
    (void)range;
    effects_.draw_attack_effect(effect, view_, projection_);
}

void Renderer::draw_paladin_aoe(float x, float y, float dir_x, float dir_y, float progress, float range) {
    ecs::AttackEffect effect;
    effect.attacker_class = PlayerClass::Paladin;
    effect.x = x;
    effect.y = y;
    effect.direction_x = dir_x;
    effect.direction_y = dir_y;
    effect.duration = 0.6f;
    effect.timer = effect.duration * (1.0f - progress);
    (void)range;
    effects_.draw_attack_effect(effect, view_, projection_);
}

void Renderer::draw_archer_arrow(float x, float y, float dir_x, float dir_y, float progress, float range) {
    ecs::AttackEffect effect;
    effect.attacker_class = PlayerClass::Archer;
    effect.x = x;
    effect.y = y;
    effect.direction_x = dir_x;
    effect.direction_y = dir_y;
    effect.duration = 0.5f;
    effect.timer = effect.duration * (1.0f - progress);
    (void)range;
    effects_.draw_attack_effect(effect, view_, projection_);
}

// ============================================================================
// UI RENDERING (delegates to UIRenderer)
// ============================================================================

void Renderer::begin_ui() {
    ui_.begin();
}

void Renderer::end_ui() {
    ui_.end();
}

void Renderer::draw_filled_rect(float x, float y, float w, float h, uint32_t color) {
    ui_.draw_filled_rect(x, y, w, h, color);
}

void Renderer::draw_rect_outline(float x, float y, float w, float h, uint32_t color, float line_width) {
    ui_.draw_rect_outline(x, y, w, h, color, line_width);
}

void Renderer::draw_circle(float x, float y, float radius, uint32_t color, int segments) {
    ui_.draw_circle(x, y, radius, color, segments);
}

void Renderer::draw_circle_outline(float x, float y, float radius, uint32_t color, 
                                    float line_width, int segments) {
    ui_.draw_circle_outline(x, y, radius, color, line_width, segments);
}

void Renderer::draw_line(float x1, float y1, float x2, float y2, uint32_t color, float line_width) {
    ui_.draw_line(x1, y1, x2, y2, color, line_width);
}

void Renderer::draw_button(float x, float y, float w, float h, const std::string& label, 
                           uint32_t color, bool selected) {
    ui_.draw_button(x, y, w, h, label, color, selected);
}

void Renderer::draw_ui_text(const std::string& text, float x, float y, float scale, uint32_t color) {
    ui_.draw_text(text, x, y, color, scale);
}

void Renderer::draw_text(const std::string& text, float x, float y, uint32_t color) {
    ui_.draw_text(text, x, y, color, 1.0f);
}

void Renderer::draw_target_reticle() {
    ui_.draw_target_reticle(context_.width(), context_.height());
}

void Renderer::draw_class_preview(PlayerClass player_class, float x, float y, float size) {
    float half = size / 2.0f;
    uint32_t color;
    
    switch (player_class) {
        case PlayerClass::Warrior: color = 0xFFC85050; break;
        case PlayerClass::Mage:    color = 0xFF5050C8; break;
        case PlayerClass::Paladin: color = 0xFFC8B450; break;
        case PlayerClass::Archer:  color = 0xFF50C850; break;
        default:                   color = 0xFFFFFFFF; break;
    }
    
    ui_.draw_filled_rect(x - half, y - half, size, size, color);
    ui_.draw_rect_outline(x - half, y - half, size, size, 0xFFFFFFFF, 2.0f);
}

// ============================================================================
// SCENE-BASED RENDERING API
// ============================================================================

void Renderer::render(const RenderScene& scene, const UIScene& ui_scene) {
    // Shadow pass first
    render_shadow_pass(scene);
    
    // Main render pass
    begin_frame();
    
    // Draw world elements based on scene flags
    if (scene.should_draw_skybox()) {
        draw_skybox();
    }
    if (scene.should_draw_mountains()) {
        draw_distant_mountains();
    }
    if (scene.should_draw_rocks()) {
        draw_rocks();
    }
    if (scene.should_draw_trees()) {
        draw_trees();
    }
    if (scene.should_draw_ground()) {
        draw_ground();
    }
    if (scene.should_draw_grass()) {
        draw_grass();
    }
    
    // Draw attack effects from scene
    for (const auto& cmd : scene.effects()) {
        draw_attack_effect(cmd.effect);
    }
    
    // Draw entities from scene
    for (const auto& cmd : scene.entities()) {
        draw_entity(cmd.state, cmd.is_local);
    }
    
    // Draw UI from scene
    begin_ui();
    render_ui(ui_scene);
    end_ui();
    
    end_frame();
}

void Renderer::render_shadow_pass(const RenderScene& scene) {
    begin_shadow_pass();
    
    // Draw world shadows based on scene flags
    if (scene.should_draw_mountain_shadows()) {
        draw_mountain_shadows();
    }
    if (scene.should_draw_tree_shadows()) {
        draw_tree_shadows();
    }
    
    // Draw entity shadows from scene
    for (const auto& cmd : scene.entity_shadows()) {
        draw_entity_shadow(cmd.state);
    }
    
    end_shadow_pass();
}

void Renderer::render_ui(const UIScene& ui_scene) {
    for (const auto& cmd : ui_scene.commands()) {
        std::visit([this](const auto& data) {
            using T = std::decay_t<decltype(data)>;
            
            if constexpr (std::is_same_v<T, FilledRectCommand>) {
                draw_filled_rect(data.x, data.y, data.w, data.h, data.color);
            }
            else if constexpr (std::is_same_v<T, RectOutlineCommand>) {
                draw_rect_outline(data.x, data.y, data.w, data.h, data.color, data.line_width);
            }
            else if constexpr (std::is_same_v<T, CircleCommand>) {
                draw_circle(data.x, data.y, data.radius, data.color, data.segments);
            }
            else if constexpr (std::is_same_v<T, CircleOutlineCommand>) {
                draw_circle_outline(data.x, data.y, data.radius, data.color, 
                                   data.line_width, data.segments);
            }
            else if constexpr (std::is_same_v<T, LineCommand>) {
                draw_line(data.x1, data.y1, data.x2, data.y2, data.color, data.line_width);
            }
            else if constexpr (std::is_same_v<T, TextCommand>) {
                draw_ui_text(data.text, data.x, data.y, data.scale, data.color);
            }
            else if constexpr (std::is_same_v<T, ButtonCommand>) {
                draw_button(data.x, data.y, data.w, data.h, data.label, data.color, data.selected);
            }
            else if constexpr (std::is_same_v<T, TargetReticleCommand>) {
                draw_target_reticle();
            }
            else if constexpr (std::is_same_v<T, PlayerHealthBarCommand>) {
                draw_player_health_ui(data.health_ratio, data.max_health);
            }
            else if constexpr (std::is_same_v<T, EnemyHealthBar3DCommand>) {
                draw_enemy_health_bar_3d(data.world_x, data.world_y, data.world_z,
                                        data.width, data.health_ratio);
            }
        }, cmd.data);
    }
}

} // namespace mmo
