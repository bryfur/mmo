#include "renderer.hpp"
#include "render/grass_renderer.hpp"
#include "render/bgfx_utils.hpp"
#include <bx/math.h>
#include <iostream>
#include <cmath>

namespace mmo {

// Use ViewId constants from render_context.hpp (included via renderer.hpp)

Renderer::Renderer() 
    : model_manager_(std::make_unique<ModelManager>()),
      grass_renderer_(std::make_unique<GrassRenderer>()) {}

Renderer::~Renderer() {
    shutdown();
}

bool Renderer::init(int width, int height, const std::string& title) {
    // Initialize render context (SDL window + bgfx)
    if (!context_.init(width, height, title)) {
        return false;
    }
    
    // Initialize vertex layouts for model loading (must be done after bgfx init)
    ModelLoader::init_vertex_layouts();
    
    // Initialize terrain renderer
    if (!terrain_.init(WORLD_WIDTH, WORLD_HEIGHT)) {
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
    
    // Initialize entity rendering shaders
    init_shaders();
    init_billboard_buffers();
    
    // Initialize grass renderer
    if (grass_renderer_) {
        grass_renderer_->init(WORLD_WIDTH, WORLD_HEIGHT);
    }
    
    return true;
}

void Renderer::init_shaders() {
    // Load model shader program
    model_program_ = bgfx_utils::load_program("model_vs", "model_fs");
    if (!bgfx::isValid(model_program_)) {
        std::cerr << "Failed to load model shader program" << std::endl;
    }
    
    // Load skinned model shader (for now use same as model, add skinning variant later)
    skinned_model_program_ = bgfx_utils::load_program("skinned_model_vs", "model_fs");
    if (!bgfx::isValid(skinned_model_program_)) {
        // Fall back to regular model program
        skinned_model_program_ = model_program_;
    }
    
    // Load billboard shader program
    billboard_program_ = bgfx_utils::load_program("billboard_vs", "billboard_fs");
    if (!bgfx::isValid(billboard_program_)) {
        std::cerr << "Failed to load billboard shader program" << std::endl;
    }
    
    // Note: u_model is a bgfx predefined uniform - use setTransform instead
    // Create only custom uniforms
    u_lightSpaceMatrix_ = bgfx::createUniform("u_lightSpaceMatrix", bgfx::UniformType::Mat4);
    u_lightDir_ = bgfx::createUniform("u_lightDir", bgfx::UniformType::Vec4);
    u_lightColor_ = bgfx::createUniform("u_lightColor", bgfx::UniformType::Vec4);
    u_ambientColor_ = bgfx::createUniform("u_ambientColor", bgfx::UniformType::Vec4);
    u_viewPos_ = bgfx::createUniform("u_viewPos", bgfx::UniformType::Vec4);
    u_fogParams_ = bgfx::createUniform("u_fogParams", bgfx::UniformType::Vec4);
    u_fogColor_ = bgfx::createUniform("u_fogColor", bgfx::UniformType::Vec4);
    u_tintColor_ = bgfx::createUniform("u_tintColor", bgfx::UniformType::Vec4);
    u_baseColor_ = bgfx::createUniform("u_baseColor", bgfx::UniformType::Vec4);
    u_params_ = bgfx::createUniform("u_params", bgfx::UniformType::Vec4);
    u_boneMatrices_ = bgfx::createUniform("u_boneMatrices", bgfx::UniformType::Mat4, MAX_BONES);
    
    // Texture samplers
    s_baseColorTexture_ = bgfx::createUniform("s_baseColorTexture", bgfx::UniformType::Sampler);
    s_shadowMap_ = bgfx::createUniform("s_shadowMap", bgfx::UniformType::Sampler);
    s_ssaoTexture_ = bgfx::createUniform("s_ssaoTexture", bgfx::UniformType::Sampler);
}

void Renderer::init_billboard_buffers() {
    // Set up vertex layout for billboards
    billboard_layout_
        .begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Float)
        .end();
}

void Renderer::shutdown() {
    if (model_manager_) {
        model_manager_->unload_all();
    }
    
    if (grass_renderer_) {
        grass_renderer_->shutdown();
    }
    
    // Destroy bgfx resources
    if (bgfx::isValid(model_program_)) {
        bgfx::destroy(model_program_);
        model_program_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(skinned_model_program_) && skinned_model_program_.idx != model_program_.idx) {
        bgfx::destroy(skinned_model_program_);
        skinned_model_program_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(billboard_program_)) {
        bgfx::destroy(billboard_program_);
        billboard_program_ = BGFX_INVALID_HANDLE;
    }
    
    // Destroy uniforms (Note: u_model is bgfx predefined, not created by us)
    if (bgfx::isValid(u_lightSpaceMatrix_)) bgfx::destroy(u_lightSpaceMatrix_);
    if (bgfx::isValid(u_lightDir_)) bgfx::destroy(u_lightDir_);
    if (bgfx::isValid(u_lightColor_)) bgfx::destroy(u_lightColor_);
    if (bgfx::isValid(u_ambientColor_)) bgfx::destroy(u_ambientColor_);
    if (bgfx::isValid(u_viewPos_)) bgfx::destroy(u_viewPos_);
    if (bgfx::isValid(u_fogParams_)) bgfx::destroy(u_fogParams_);
    if (bgfx::isValid(u_fogColor_)) bgfx::destroy(u_fogColor_);
    if (bgfx::isValid(u_tintColor_)) bgfx::destroy(u_tintColor_);
    if (bgfx::isValid(u_baseColor_)) bgfx::destroy(u_baseColor_);
    if (bgfx::isValid(u_params_)) bgfx::destroy(u_params_);
    if (bgfx::isValid(u_boneMatrices_)) bgfx::destroy(u_boneMatrices_);
    if (bgfx::isValid(s_baseColorTexture_)) bgfx::destroy(s_baseColorTexture_);
    if (bgfx::isValid(s_shadowMap_)) bgfx::destroy(s_shadowMap_);
    if (bgfx::isValid(s_ssaoTexture_)) bgfx::destroy(s_ssaoTexture_);
    
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
    
    // NPC models
    model_manager_->load_model("npc", models_path + "npc.glb");
    model_manager_->load_model("npc_merchant", models_path + "npc_merchant.glb");
    model_manager_->load_model("npc_guard", models_path + "npc_guard.glb");
    model_manager_->load_model("npc_blacksmith", models_path + "npc_blacksmith.glb");
    model_manager_->load_model("npc_innkeeper", models_path + "npc_innkeeper.glb");
    model_manager_->load_model("npc_villager", models_path + "npc_villager.glb");
    
    // Building models
    model_manager_->load_model("building_tavern", models_path + "building_tavern.glb");
    model_manager_->load_model("building_blacksmith", models_path + "building_blacksmith.glb");
    model_manager_->load_model("building_tower", models_path + "building_tower.glb");
    model_manager_->load_model("building_shop", models_path + "building_shop.glb");
    model_manager_->load_model("building_well", models_path + "building_well.glb");
    model_manager_->load_model("building_house", models_path + "building_house.glb");
    
    // Attack effect models
    model_manager_->load_model("sword_slash", models_path + "sword_slash.glb");
    model_manager_->load_model("fireball", models_path + "fireball.glb");
    model_manager_->load_model("paladin_aoe", models_path + "paladin_aoe.glb");
    model_manager_->load_model("arrow", models_path + "arrow.glb");
    
    models_loaded_ = true;
    return success;
}

// ============================================================================
// FRAME MANAGEMENT
// ============================================================================

void Renderer::begin_frame() {
    // Update camera matrices
    update_camera();
    
    // Set up main view
    bgfx::setViewRect(ViewId::Main, 0, 0, uint16_t(context_.width()), uint16_t(context_.height()));
    bgfx::setViewClear(ViewId::Main, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x303030FF, 1.0f, 0);
    
    // Convert GLM matrices to bgfx format (column-major float arrays)
    float view_mtx[16];
    float proj_mtx[16];
    memcpy(view_mtx, &view_[0][0], sizeof(float) * 16);
    memcpy(proj_mtx, &projection_[0][0], sizeof(float) * 16);
    
    bgfx::setViewTransform(ViewId::Main, view_mtx, proj_mtx);
    
    // Touch the view to ensure it's cleared even if nothing is rendered
    bgfx::touch(ViewId::Main);
}

void Renderer::end_frame() {
    bgfx::frame();
}

// ============================================================================
// SHADOW PASS
// ============================================================================

void Renderer::begin_shadow_pass() {
    shadows_.begin_shadow_pass();
}

void Renderer::end_shadow_pass() {
    shadows_.end_shadow_pass();
}

void Renderer::draw_entity_shadow(const EntityState& entity) {
    Model* model = get_model_for_entity(entity);
    if (!model) return;
    
    float terrain_y = terrain_.get_height(entity.x, entity.y);
    glm::vec3 position(entity.x, terrain_y, entity.y);
    
    float rotation = 0.0f;
    if (entity.attack_dir_x != 0.0f || entity.attack_dir_y != 0.0f) {
        rotation = std::atan2(-entity.attack_dir_x, -entity.attack_dir_y);
    } else if (entity.vx != 0.0f || entity.vy != 0.0f) {
        rotation = std::atan2(-entity.vx, -entity.vy);
    }
    
    float scale = 50.0f;
    if (entity.type == EntityType::Building) {
        scale = 250.0f;
    }
    
    draw_model_shadow(model, position, rotation, scale);
}

void Renderer::draw_model_shadow(Model* model, const glm::vec3& position, float rotation, float scale) {
    if (!model || !shadows_.is_enabled()) return;
    
    glm::mat4 model_mat = glm::mat4(1.0f);
    model_mat = glm::translate(model_mat, position);
    model_mat = glm::rotate(model_mat, rotation, glm::vec3(0.0f, 1.0f, 0.0f));
    model_mat = glm::scale(model_mat, glm::vec3(scale));
    
    float cx = (model->min_x + model->max_x) / 2.0f;
    float cy = model->min_y;
    float cz = (model->min_z + model->max_z) / 2.0f;
    model_mat = model_mat * glm::translate(glm::mat4(1.0f), glm::vec3(-cx, -cy, -cz));
    
    // Submit to shadow pass using shadow program
    bgfx::setUniform(shadows_.u_model(), &model_mat[0][0]);
    glm::mat4 lsm = shadows_.light_space_matrix();
    bgfx::setUniform(shadows_.u_lightSpaceMatrix(), &lsm[0][0]);
    
    bool is_skinned = model->has_skeleton && bgfx::isValid(shadows_.skinned_shadow_program());
    bgfx::ProgramHandle program = is_skinned ? shadows_.skinned_shadow_program() : shadows_.shadow_program();
    
    for (auto& mesh : model->meshes) {
        if (!mesh.uploaded) {
            ModelLoader::upload_to_gpu(*model);
        }
        
        bgfx::setVertexBuffer(0, mesh.vbh);
        if (bgfx::isValid(mesh.ibh)) {
            bgfx::setIndexBuffer(mesh.ibh);
        }
        
        uint64_t state = BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_CULL_CCW;
        bgfx::setState(state);
        bgfx::submit(ViewId::Shadow, program);
    }
}

void Renderer::draw_mountain_shadows() {
    if (!shadows_.is_enabled()) return;
    
    Model* mountain_large = model_manager_->get_model("mountain_large");
    if (!mountain_large) return;
    
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
    if (!shadows_.is_enabled()) return;
    
    Model* trees[2] = {
        model_manager_->get_model("tree_oak"),
        model_manager_->get_model("tree_pine")
    };
    
    bool any_tree = trees[0] || trees[1];
    if (!any_tree) return;
    
    auto tree_positions = world_.get_tree_positions_for_shadows();
    for (const auto& tp : tree_positions) {
        // Only render shadows for trees within reasonable distance
        float dx = tp.x - camera_x_;
        float dz = tp.z - camera_y_;
        float dist_sq = dx * dx + dz * dz;
        if (dist_sq > 3000.0f * 3000.0f) continue;
        
        Model* tree = (tp.tree_type < 2 && trees[tp.tree_type]) ? trees[tp.tree_type] : trees[0];
        if (!tree) continue;
        
        float terrain_y = terrain_.get_height(tp.x, tp.z);
        glm::vec3 pos(tp.x, terrain_y, tp.z);
        draw_model_shadow(tree, pos, glm::radians(tp.rotation), tp.scale);
    }
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

bool Renderer::get_shadows_enabled() const {
    return shadows_.is_enabled();
}

bool Renderer::get_ssao_enabled() const {
    return ssao_.is_enabled();
}

// ============================================================================
// WORLD RENDERING (delegates to subsystems)
// ============================================================================

void Renderer::draw_skybox() {
    if (!skybox_enabled_) return;
    skybox_time_ += 0.016f;
    world_.update(0.016f);
    world_.render_skybox(ViewId::Main, view_, projection_);
}

void Renderer::draw_distant_mountains() {
    if (!mountains_enabled_) return;
    world_.render_mountains(ViewId::Main, view_, projection_, actual_camera_pos_, light_dir_);
}

void Renderer::draw_rocks() {
    if (!rocks_enabled_) return;
    world_.render_rocks(ViewId::Main, view_, projection_, actual_camera_pos_,
                        shadows_.light_space_matrix(), shadows_.shadow_depth_texture(),
                        shadows_.is_enabled(), ssao_.ssao_texture(), ssao_.is_enabled(),
                        light_dir_, glm::vec2(context_.width(), context_.height()));
}

void Renderer::draw_trees() {
    if (!trees_enabled_) return;
    world_.render_trees(ViewId::Main, view_, projection_, actual_camera_pos_,
                        shadows_.light_space_matrix(), shadows_.shadow_depth_texture(),
                        shadows_.is_enabled(), ssao_.ssao_texture(), ssao_.is_enabled(),
                        light_dir_, glm::vec2(context_.width(), context_.height()));
}

void Renderer::draw_ground() {
    terrain_.render(ViewId::Main, view_, projection_, actual_camera_pos_,
                    shadows_.light_space_matrix(), shadows_.shadow_depth_texture(),
                    shadows_.is_enabled(), ssao_.ssao_texture(), ssao_.is_enabled(),
                    light_dir_, glm::vec2(context_.width(), context_.height()));
}

void Renderer::draw_grass() {
    if (!grass_renderer_ || !grass_enabled_) return;
    
    grass_renderer_->update(0.016f, skybox_time_);
    grass_renderer_->render(ViewId::Main, view_, projection_, actual_camera_pos_,
                           shadows_.light_space_matrix(), shadows_.shadow_depth_texture(),
                           shadows_.is_enabled(), light_dir_);
}

void Renderer::draw_grid() {
    world_.render_grid(ViewId::Main, view_, projection_);
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
            case BuildingType::Tavern:     return model_manager_->get_model("building_tavern");
            case BuildingType::Blacksmith: return model_manager_->get_model("building_blacksmith");
            case BuildingType::Tower:      return model_manager_->get_model("building_tower");
            case BuildingType::Shop:       return model_manager_->get_model("building_shop");
            case BuildingType::Well:       return model_manager_->get_model("building_well");
            case BuildingType::House:      return model_manager_->get_model("building_house");
            default:                       return model_manager_->get_model("building_house");
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
    if (entity.type == EntityType::Player) {
        rotation = std::atan2(entity.attack_dir_x, entity.attack_dir_y);
    } else if (entity.type != EntityType::Building && (entity.vx != 0.0f || entity.vy != 0.0f)) {
        rotation = std::atan2(entity.vx, entity.vy);
    }
    
    float target_size;
    bool show_health_bar = true;
    
    switch (entity.type) {
        case EntityType::Building:
            switch (entity.building_type) {
                case BuildingType::Tower:      target_size = 160.0f; break;
                case BuildingType::Tavern:     target_size = 140.0f; break;
                case BuildingType::Blacksmith: target_size = 120.0f; break;
                case BuildingType::Shop:       target_size = 100.0f; break;
                case BuildingType::House:      target_size = 110.0f; break;
                case BuildingType::Well:       target_size = 60.0f; break;
                default:                       target_size = 100.0f; break;
            }
            show_health_bar = false;
            break;
        case EntityType::TownNPC:
            target_size = PLAYER_SIZE * 0.9f;
            show_health_bar = false;
            break;
        case EntityType::NPC:
            target_size = NPC_SIZE;
            break;
        default:
            target_size = PLAYER_SIZE;
            break;
    }
    
    float model_size = model->max_dimension();
    float scale = (target_size * 1.5f) / model_size;
    
    float terrain_y = terrain_.get_height(entity.x, entity.y);
    glm::vec3 position(entity.x, terrain_y, entity.y);
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
        float bar_height_offset = terrain_y + target_size * 1.3f;
        draw_enemy_health_bar_3d(entity.x, bar_height_offset, entity.y, target_size * 0.8f, health_ratio);
    }
}

void Renderer::draw_player(const PlayerState& player, bool is_local) {
    draw_entity(player, is_local);
}

void Renderer::render_model_bgfx(Model* model, const glm::mat4& model_mat, 
                                  const glm::vec4& tint, bool use_fog, bool use_shadows, bool use_ssao) {
    if (!model || !bgfx::isValid(model_program_)) return;
    
    bool is_skinned = model->has_skeleton && bgfx::isValid(skinned_model_program_);
    bgfx::ProgramHandle program = is_skinned ? skinned_model_program_ : model_program_;
    
    // Set model matrix using bgfx's predefined u_model
    bgfx::setTransform(&model_mat[0][0]);
    
    // Set view/projection (done via setViewTransform in begin_frame)
    
    // Set lighting
    float lightDir[4] = { light_dir_.x, light_dir_.y, light_dir_.z, 0.0f };
    float lightColor[4] = { 1.0f, 0.95f, 0.9f, 1.0f };
    float ambientColor[4] = { 0.4f, 0.4f, 0.5f, 1.0f };
    float viewPos[4] = { actual_camera_pos_.x, actual_camera_pos_.y, actual_camera_pos_.z, 1.0f };
    
    bgfx::setUniform(u_lightDir_, lightDir);
    bgfx::setUniform(u_lightColor_, lightColor);
    bgfx::setUniform(u_ambientColor_, ambientColor);
    bgfx::setUniform(u_viewPos_, viewPos);
    
    // Set fog parameters
    float fogColor[4] = { 0.35f, 0.45f, 0.6f, 1.0f };
    float fogParams[4] = { 800.0f, 4000.0f, 0.0f, use_fog && fog_enabled_ ? 1.0f : 0.0f };
    bgfx::setUniform(u_fogColor_, fogColor);
    bgfx::setUniform(u_fogParams_, fogParams);
    
    // Set tint
    float tintColor[4] = { tint.r, tint.g, tint.b, tint.a };
    bgfx::setUniform(u_tintColor_, tintColor);
    
    // Set shadow/SSAO params
    glm::mat4 lsm = shadows_.light_space_matrix();
    bgfx::setUniform(u_lightSpaceMatrix_, &lsm[0][0]);
    
    float params[4] = { 
        0.0f,  // hasTexture (set per mesh)
        (use_shadows && shadows_.is_enabled()) ? 1.0f : 0.0f,
        (use_ssao && ssao_.is_enabled()) ? 1.0f : 0.0f,
        0.0f   // useSkinning
    };
    
    // Set textures for shadow/SSAO
    bgfx::setTexture(2, s_shadowMap_, shadows_.shadow_depth_texture());
    bgfx::setTexture(3, s_ssaoTexture_, ssao_.ssao_texture());
    
    // Skinned animation matrices
    if (is_skinned) {
        AnimationState* anim_state = nullptr;
        static const char* animated_models[] = {"warrior", "mage", "paladin"};
        for (const char* name : animated_models) {
            if (model_manager_->get_model(name) == model) {
                anim_state = model_manager_->get_animation_state(name);
                break;
            }
        }
        
        if (anim_state) {
            params[3] = 1.0f;  // useSkinning = true
            bgfx::setUniform(u_boneMatrices_, &anim_state->bone_matrices[0][0][0], MAX_BONES);
        }
    }
    
    // Render each mesh
    for (auto& mesh : model->meshes) {
        if (!mesh.uploaded) {
            ModelLoader::upload_to_gpu(*model);
        }
        
        if (!bgfx::isValid(mesh.vbh) || !bgfx::isValid(mesh.ibh)) continue;
        
        // Set hasTexture param
        params[0] = (mesh.has_texture && bgfx::isValid(mesh.texture)) ? 1.0f : 0.0f;
        bgfx::setUniform(u_params_, params);
        
        // Set base color texture
        if (mesh.has_texture && bgfx::isValid(mesh.texture)) {
            bgfx::setTexture(0, s_baseColorTexture_, mesh.texture);
        }
        
        // Set vertex/index buffers
        bgfx::setVertexBuffer(0, mesh.vbh);
        bgfx::setIndexBuffer(mesh.ibh);
        
        // Set render state
        uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z |
                         BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_CULL_CW | BGFX_STATE_MSAA;
        bgfx::setState(state);
        
        // Submit
        bgfx::submit(ViewId::Main, program);
    }
}

void Renderer::draw_model(Model* model, const glm::vec3& position, float rotation, float scale, 
                          const glm::vec4& tint, float attack_tilt) {
    if (!model) return;
    
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
    
    render_model_bgfx(model, model_mat, tint, true, true, true);
}

void Renderer::draw_model_no_fog(Model* model, const glm::vec3& position, float rotation, 
                                  float scale, const glm::vec4& tint) {
    if (!model) return;
    
    glm::mat4 model_mat = glm::mat4(1.0f);
    model_mat = glm::translate(model_mat, position);
    model_mat = glm::rotate(model_mat, rotation, glm::vec3(0.0f, 1.0f, 0.0f));
    model_mat = glm::scale(model_mat, glm::vec3(scale));
    
    float cx = (model->min_x + model->max_x) / 2.0f;
    float cy = model->min_y;
    float cz = (model->min_z + model->max_z) / 2.0f;
    model_mat = model_mat * glm::translate(glm::mat4(1.0f), glm::vec3(-cx, -cy, -cz));
    
    render_model_bgfx(model, model_mat, tint, false, false, false);
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
    // Project world position to clip space
    glm::vec4 world_pos(world_x, world_y, world_z, 1.0f);
    glm::vec4 clip_pos = projection_ * view_ * world_pos;
    if (clip_pos.w <= 0.01f) return;
    
    glm::vec3 ndc = glm::vec3(clip_pos) / clip_pos.w;
    if (ndc.x < -1.5f || ndc.x > 1.5f || ndc.y < -1.5f || ndc.y > 1.5f || ndc.z < -1.0f || ndc.z > 1.0f) {
        return;
    }
    
    if (!bgfx::isValid(billboard_program_)) return;
    
    // Calculate screen-space position
    float screen_x = (ndc.x + 1.0f) * 0.5f * context_.width();
    float screen_y = (1.0f - ndc.y) * 0.5f * context_.height();  // Y is flipped
    
    // Calculate bar size based on distance
    float distance = clip_pos.w;
    float base_scale = 100.0f / std::max(distance, 100.0f);
    float pixel_width = bar_width * base_scale * 2.0f;
    float pixel_height = bar_width * 0.2f * base_scale * 2.0f;
    
    // Clamp minimum size
    pixel_width = std::max(pixel_width, 30.0f);
    pixel_height = std::max(pixel_height, 6.0f);
    
    // Draw using UI renderer (2D)
    float bar_x = screen_x - pixel_width / 2.0f;
    float bar_y = screen_y - pixel_height / 2.0f;
    
    // Background
    ui_.draw_filled_rect(bar_x - 1, bar_y - 1, pixel_width + 2, pixel_height + 2, 0xCC000000);
    
    // Empty part (dark red)
    ui_.draw_filled_rect(bar_x, bar_y, pixel_width, pixel_height, 0xE6660000);
    
    // Filled part (green)
    float fill_width = pixel_width * health_ratio;
    ui_.draw_filled_rect(bar_x, bar_y, fill_width, pixel_height, 0xFF00CC00);
}

// ============================================================================
// ATTACK EFFECTS (delegates to EffectRenderer)
// ============================================================================

void Renderer::draw_attack_effect(const ecs::AttackEffect& effect) {
    effects_.draw_attack_effect(effect, ViewId::Main, view_, projection_);
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
    effects_.draw_attack_effect(effect, ViewId::Main, view_, projection_);
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
    effects_.draw_attack_effect(effect, ViewId::Main, view_, projection_);
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
    effects_.draw_attack_effect(effect, ViewId::Main, view_, projection_);
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
    effects_.draw_attack_effect(effect, ViewId::Main, view_, projection_);
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

} // namespace mmo
