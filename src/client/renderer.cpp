#include "renderer.hpp"
#include "render/grass_renderer.hpp"
#include "render/text_renderer.hpp"
#include "render_constants.hpp"
#include "gpu/gpu_uniforms.hpp"
#include <glm/gtc/type_ptr.hpp>
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

bool Renderer::init(int width, int height, const std::string& title, float world_width, float world_height) {
    // Initialize render context (SDL window + GPU device)
    if (!context_.init(width, height, title)) {
        return false;
    }
    
    // Initialize pipeline registry for SDL3 GPU pipelines
    if (!pipeline_registry_.init(context_.device())) {
        std::cerr << "Failed to initialize pipeline registry" << std::endl;
        return false;
    }
    pipeline_registry_.set_swapchain_format(context_.swapchain_format());
    
    // Initialize terrain renderer with GPU device and pipeline registry
    if (!terrain_.init(context_.device(), pipeline_registry_, world_width, world_height)) {
        std::cerr << "Failed to initialize terrain renderer" << std::endl;
        return false;
    }
    
    // Initialize world renderer (skybox, mountains, rocks, trees, grid)
    if (!world_.init(context_.device(), pipeline_registry_, world_width, world_height, model_manager_.get())) {
        std::cerr << "Failed to initialize world renderer" << std::endl;
        return false;
    }
    
    // Set terrain height callback for world objects
    world_.set_terrain_height_func([this](float x, float z) {
        return terrain_.get_height(x, z);
    });
    
    // Initialize UI renderer with SDL3 GPU resources
    if (!ui_.init(context_.device(), pipeline_registry_, width, height)) {
        std::cerr << "Failed to initialize UI renderer" << std::endl;
        return false;
    }
    
    // Initialize effect renderer with SDL3 GPU resources
    if (!effects_.init(context_.device(), pipeline_registry_, model_manager_.get())) {
        std::cerr << "Failed to initialize effect renderer" << std::endl;
        return false;
    }
    effects_.set_terrain_height_func([this](float x, float z) {
        return terrain_.get_height(x, z);
    });
    
    // Create depth texture for main 3D render pass
    depth_texture_ = gpu::GPUTexture::create_depth(context_.device(), width, height);
    if (!depth_texture_) {
        std::cerr << "Failed to create depth texture" << std::endl;
        return false;
    }

    // Initialize GPU resources for entity rendering
    init_pipelines();
    init_billboard_buffers();
    
    // Initialize grass renderer (SDL3 GPU API)
    if (grass_renderer_) {
        grass_renderer_->set_terrain_height_func([this](float x, float z) {
            return terrain_.get_height(x, z);
        });
        grass_renderer_->init(context_.device(), pipeline_registry_, world_width, world_height);
    }
    
    return true;
}

void Renderer::init_pipelines() {
    // Preload commonly used pipelines to avoid hitching during gameplay
    // Note: Pipeline creation can return nullptr on failure, but we continue
    // as the pipelines will be created on-demand when first needed
    auto* model_pipeline = pipeline_registry_.get_model_pipeline();
    auto* skinned_pipeline = pipeline_registry_.get_skinned_model_pipeline();
    auto* billboard_pipeline = pipeline_registry_.get_billboard_pipeline();
    
    if (!model_pipeline || !skinned_pipeline || !billboard_pipeline) {
        std::cerr << "Warning: Some pipelines failed to preload" << std::endl;
    }
    
    // Create default sampler for model textures
    // Note: This sampler is prepared for future use when model rendering
    // transitions from GL to SDL3 GPU API (issue #14)
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
    
    if (!default_sampler_) {
        std::cerr << "Warning: Failed to create default GPU sampler" << std::endl;
    }
}

void Renderer::init_billboard_buffers() {
    // Billboard vertex buffer for GPU rendering
    constexpr size_t BILLBOARD_BUFFER_SIZE = 6 * 7 * sizeof(float);
    billboard_vertex_buffer_ = gpu::GPUBuffer::create_dynamic(
        context_.device(), 
        gpu::GPUBuffer::Type::Vertex, 
        BILLBOARD_BUFFER_SIZE
    );
    
    if (!billboard_vertex_buffer_) {
        std::cerr << "Warning: Failed to create billboard vertex buffer" << std::endl;
    }
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
    depth_texture_.reset();

    if (default_sampler_) {
        context_.device().release_sampler(default_sampler_);
        default_sampler_ = nullptr;
    }
    
    // Shutdown subsystems first (they use pipeline_registry)
    effects_.shutdown();
    ui_.shutdown();
    world_.shutdown();
    terrain_.shutdown();
    // Shutdown pipeline registry before device
    pipeline_registry_.shutdown();
    
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

    // Reset per-frame state
    had_main_pass_this_frame_ = false;

    // Update camera system screen size
    camera_system_.set_screen_size(context_.width(), context_.height());
    ui_.set_screen_size(context_.width(), context_.height());
}

void Renderer::end_frame() {
    // Text texture creation is now handled in UIRenderer::execute()
    // Clear per-frame state
    current_swapchain_ = nullptr;
    context_.end_frame();
}

void Renderer::begin_main_pass() {
    SDL_GPUCommandBuffer* cmd = context_.current_command_buffer();
    if (!cmd) {
        std::cerr << "begin_main_pass: No active command buffer" << std::endl;
        return;
    }

    // Acquire swapchain texture for color target (stored for reuse in UI pass)
    uint32_t sw_width, sw_height;
    current_swapchain_ = context_.acquire_swapchain_texture(cmd, &sw_width, &sw_height);
    if (!current_swapchain_) {
        std::cerr << "begin_main_pass: Failed to acquire swapchain texture" << std::endl;
        return;
    }

    // Check if depth texture needs resize (window resize)
    if (depth_texture_ && (depth_texture_->width() != static_cast<int>(sw_width) ||
                           depth_texture_->height() != static_cast<int>(sw_height))) {
        depth_texture_ = gpu::GPUTexture::create_depth(context_.device(), sw_width, sw_height);
        if (!depth_texture_) {
            std::cerr << "begin_main_pass: Failed to resize depth texture" << std::endl;
            return;
        }
    }

    // Configure color target (clear to sky/fog color)
    SDL_GPUColorTargetInfo color_target = {};
    color_target.texture = current_swapchain_;
    color_target.load_op = SDL_GPU_LOADOP_CLEAR;
    color_target.store_op = SDL_GPU_STOREOP_STORE;
    color_target.clear_color = { 0.35f, 0.45f, 0.6f, 1.0f };  // Fog/sky color

    // Configure depth target
    SDL_GPUDepthStencilTargetInfo depth_target = {};
    depth_target.texture = depth_texture_ ? depth_texture_->handle() : nullptr;
    depth_target.load_op = SDL_GPU_LOADOP_CLEAR;
    depth_target.store_op = SDL_GPU_STOREOP_STORE;
    depth_target.clear_depth = 1.0f;
    depth_target.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    depth_target.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;

    // Begin main 3D render pass
    main_render_pass_ = SDL_BeginGPURenderPass(cmd, &color_target, 1,
                                                depth_texture_ ? &depth_target : nullptr);
    if (!main_render_pass_) {
        std::cerr << "begin_main_pass: Failed to begin render pass" << std::endl;
        return;
    }

    // Mark that we had a main pass this frame (for UI to know whether to clear)
    had_main_pass_this_frame_ = true;
}

void Renderer::end_main_pass() {
    if (main_render_pass_) {
        SDL_EndGPURenderPass(main_render_pass_);
        main_render_pass_ = nullptr;
    }
}

// ============================================================================
// CAMERA
// ============================================================================

void Renderer::set_camera(float x, float z) {
    player_x_ = x;
    player_z_ = z;
}

void Renderer::set_camera_velocity(float vx, float vz) {
    camera_system_.set_target_velocity(glm::vec3(vx, 0.0f, vz));
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

    float terrain_y = terrain_.get_height(player_x_, player_z_);
    camera_system_.set_target(glm::vec3(player_x_, terrain_y, player_z_));
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

    float terrain_y = terrain_.get_height(player_x_, player_z_);
    camera_system_.set_target(glm::vec3(player_x_, terrain_y, player_z_));
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
        
        if (!default_sampler_) {
            std::cerr << "Warning: Failed to recreate default GPU sampler (anisotropic level: " 
                      << level << ")" << std::endl;
        }
    }
}

void Renderer::set_heightmap(const HeightmapChunk& heightmap) {
    // Pass heightmap to terrain renderer for GPU upload
    terrain_.set_heightmap(heightmap);
    
    // Note: GrassRenderer now uses SDL3 GPU API but requires the main render pass
    // integration to render (see issue #13). Height queries are wired for placement.
    
    std::cout << "[Renderer] Heightmap set for terrain rendering" << std::endl;
}

// ============================================================================
// WORLD RENDERING (delegates to subsystems)
// ============================================================================

void Renderer::draw_skybox() {
    if (!skybox_enabled_) return;
    skybox_time_ += 0.016f;
    world_.update(0.016f);

    if (main_render_pass_) {
        SDL_GPUCommandBuffer* cmd = context_.current_command_buffer();
        if (cmd) {
            world_.render_skybox(main_render_pass_, cmd, view_, projection_);
        }
    }
}

void Renderer::draw_distant_mountains() {
    if (!mountains_enabled_) return;
    if (main_render_pass_) {
        SDL_GPUCommandBuffer* cmd = context_.current_command_buffer();
        if (cmd) {
            world_.render_mountains(main_render_pass_, cmd, view_, projection_,
                                    actual_camera_pos_, light_dir_);
        }
    }
}


void Renderer::draw_ground() {
    // Use main render pass if available (SDL3 GPU path)
    if (main_render_pass_) {
        SDL_GPUCommandBuffer* cmd = context_.current_command_buffer();
        if (cmd) {
            terrain_.render(main_render_pass_, cmd, view_, projection_, actual_camera_pos_,
                           light_dir_);
        }
    }
}

void Renderer::draw_grass() {
    if (!grass_renderer_ || !grass_enabled_) return;

    grass_renderer_->update(0.016f, skybox_time_);

    // Use main render pass if available (SDL3 GPU path)
    if (main_render_pass_) {
        SDL_GPUCommandBuffer* cmd = context_.current_command_buffer();
        if (cmd) {
            grass_renderer_->render(main_render_pass_, cmd, view_, projection_,
                                    actual_camera_pos_, light_dir_);
        }
    }
}

void Renderer::draw_grid() {
    if (main_render_pass_) {
        SDL_GPUCommandBuffer* cmd = context_.current_command_buffer();
        if (cmd) {
            world_.render_grid(main_render_pass_, cmd, view_, projection_);
        }
    }
}

// ============================================================================
// ENTITY RENDERING
// ============================================================================

Model* Renderer::get_model_for_entity(const EntityState& entity) {
    // Use server-provided model_name directly
    if (entity.model_name[0] != '\0') {
        return model_manager_->get_model(entity.model_name);
    }
    return nullptr;
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
    
    // Use server-provided target_size directly
    float target_size = entity.target_size;
    bool show_health_bar = (entity.type != EntityType::Building &&
                            entity.type != EntityType::Environment &&
                            entity.type != EntityType::TownNPC);
    
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
    
    if (model->has_skeleton && entity.model_name[0] != '\0') {
        std::string anim_model_name = entity.model_name;

        std::string anim_name;
        if (entity.is_attacking) {
            anim_name = "Attack";
        } else if (std::abs(entity.vx) > 1.0f || std::abs(entity.vy) > 1.0f) {
            anim_name = "Walk";
        } else {
            anim_name = "Idle";
        }
        set_entity_animation(anim_model_name, anim_name);
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
                          const glm::vec4& tint, float attack_tilt, bool enable_fog) {
    if (!model || !main_render_pass_) return;

    SDL_GPUCommandBuffer* cmd = context_.current_command_buffer();
    if (!cmd) return;

    gpu::GPUPipeline* pipeline = model->has_skeleton
        ? pipeline_registry_.get_skinned_model_pipeline()
        : pipeline_registry_.get_model_pipeline();

    if (!pipeline) return;

    // Build model matrix
    glm::mat4 model_mat = glm::mat4(1.0f);
    model_mat = glm::translate(model_mat, position);
    model_mat = glm::rotate(model_mat, rotation, glm::vec3(0.0f, 1.0f, 0.0f));
    if (attack_tilt != 0.0f) {
        model_mat = glm::rotate(model_mat, attack_tilt, glm::vec3(1.0f, 0.0f, 0.0f));
    }
    model_mat = glm::scale(model_mat, glm::vec3(scale));

    // Center model on its base
    float cx = (model->min_x + model->max_x) / 2.0f;
    float cy = model->min_y;
    float cz = (model->min_z + model->max_z) / 2.0f;
    model_mat = model_mat * glm::translate(glm::mat4(1.0f), glm::vec3(-cx, -cy, -cz));

    glm::mat4 normal_mat = glm::transpose(glm::inverse(model_mat));

    // Vertex uniforms
    gpu::ModelTransformUniforms transform_uniforms = {};
    transform_uniforms.model = model_mat;
    transform_uniforms.view = view_;
    transform_uniforms.projection = projection_;
    transform_uniforms.cameraPos = actual_camera_pos_;
    transform_uniforms.normalMatrix = normal_mat;
    transform_uniforms.useSkinning = model->has_skeleton ? 1 : 0;

    // Fragment uniforms
    bool fog_active = enable_fog && fog_enabled_;
    gpu::ModelLightingUniforms lighting_uniforms = {};
    lighting_uniforms.lightDir = light_dir_;
    lighting_uniforms.lightColor = lighting::LIGHT_COLOR;
    lighting_uniforms.ambientColor = fog_active ? lighting::AMBIENT_COLOR : lighting::AMBIENT_COLOR_NO_FOG;
    lighting_uniforms.tintColor = tint;
    lighting_uniforms.fogColor = fog_active ? fog::COLOR : fog::DISTANT_COLOR;
    lighting_uniforms.fogStart = fog_active ? fog::START : fog::DISTANT_START;
    lighting_uniforms.fogEnd = fog_active ? fog::END : fog::DISTANT_END;
    lighting_uniforms.fogEnabled = fog_active ? 1 : 0;

    pipeline->bind(main_render_pass_);
    SDL_PushGPUVertexUniformData(cmd, 0, &transform_uniforms, sizeof(transform_uniforms));

    // For skinned models, push bone matrices
    if (model->has_skeleton) {
        AnimationState* anim_state = nullptr;
        static const char* animated_models[] = {"warrior", "mage", "paladin", "archer"};
        for (const char* name : animated_models) {
            if (model_manager_->get_model(name) == model) {
                anim_state = model_manager_->get_animation_state(name);
                break;
            }
        }

        if (anim_state) {
            SDL_PushGPUVertexUniformData(cmd, 1, anim_state->bone_matrices.data(),
                                          MAX_BONES * sizeof(glm::mat4));
        }
    }

    // Draw each mesh
    for (auto& mesh : model->meshes) {
        if (!mesh.uploaded) {
            ModelLoader::upload_to_gpu(context_.device(), *model);
        }

        if (!mesh.vertex_buffer || !mesh.index_buffer) continue;
        if (mesh.indices.empty()) continue;

        lighting_uniforms.hasTexture = (mesh.has_texture && mesh.texture) ? 1 : 0;
        SDL_PushGPUFragmentUniformData(cmd, 0, &lighting_uniforms, sizeof(lighting_uniforms));

        if (mesh.has_texture && mesh.texture && default_sampler_) {
            SDL_GPUTextureSamplerBinding tex_binding = { mesh.texture->handle(), default_sampler_ };
            SDL_BindGPUFragmentSamplers(main_render_pass_, 0, &tex_binding, 1);
        }

        mesh.bind_buffers(main_render_pass_);
        SDL_DrawGPUIndexedPrimitives(main_render_pass_,
                                      static_cast<uint32_t>(mesh.indices.size()),
                                      1, 0, 0, 0);
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
    // Project world position to screen space and draw as 2D UI element
    // This avoids GPU buffer updates during the render pass
    glm::vec4 world_pos(world_x, world_y, world_z, 1.0f);
    glm::vec4 clip_pos = projection_ * view_ * world_pos;
    if (clip_pos.w <= 0.01f) return;

    glm::vec3 ndc = glm::vec3(clip_pos) / clip_pos.w;
    if (ndc.x < -1.5f || ndc.x > 1.5f || ndc.y < -1.5f || ndc.y > 1.5f || ndc.z < -1.0f || ndc.z > 1.0f) {
        return;
    }

    // Convert NDC to screen coordinates
    float screen_x = (ndc.x * 0.5f + 0.5f) * context_.width();
    float screen_y = (1.0f - (ndc.y * 0.5f + 0.5f)) * context_.height();  // Flip Y for screen coords

    // Scale bar size based on distance (perspective)
    float distance_scale = 100.0f / clip_pos.w;  // Smaller when further away
    distance_scale = glm::clamp(distance_scale, 0.3f, 1.5f);

    float bar_w = bar_width * 2.0f * distance_scale;
    float bar_h = bar_width * 0.4f * distance_scale;

    // Center the bar
    float x = screen_x - bar_w * 0.5f;
    float y = screen_y - bar_h * 0.5f;

    // Queue draws to UI renderer (will be rendered during UI pass)
    // Background
    ui_.draw_filled_rect(x - 1, y - 1, bar_w + 2, bar_h + 2, ui_colors::HEALTH_3D_BG);
    // Empty
    ui_.draw_filled_rect(x, y, bar_w, bar_h, ui_colors::HEALTH_BAR_BG);
    // Health fill
    float fill_w = bar_w * health_ratio;
    ui_.draw_filled_rect(x, y, fill_w, bar_h, ui_colors::HEALTH_HIGH);
}

// ============================================================================
// ATTACK EFFECTS (delegates to EffectRenderer)
// ============================================================================

void Renderer::draw_attack_effect(const ecs::AttackEffect& effect) {
    if (!main_render_pass_ || !context_.current_command_buffer()) return;

    effects_.draw_attack_effect(
        main_render_pass_,
        context_.current_command_buffer(),
        effect,
        view_,
        projection_,
        actual_camera_pos_
    );
}

void Renderer::draw_warrior_slash(float x, float y, float dir_x, float dir_y, float progress) {
    ecs::AttackEffect effect;
    effect.effect_type = "melee_swing";
    effect.x = x;
    effect.y = y;
    effect.direction_x = dir_x;
    effect.direction_y = dir_y;
    effect.duration = 0.3f;
    effect.timer = effect.duration * (1.0f - progress);
    effect.range = 60.0f;
    draw_attack_effect(effect);
}

void Renderer::draw_mage_beam(float x, float y, float dir_x, float dir_y, float progress, float range) {
    ecs::AttackEffect effect;
    effect.effect_type = "projectile";
    effect.x = x;
    effect.y = y;
    effect.direction_x = dir_x;
    effect.direction_y = dir_y;
    effect.duration = 0.4f;
    effect.timer = effect.duration * (1.0f - progress);
    effect.range = range;
    draw_attack_effect(effect);
}

void Renderer::draw_paladin_aoe(float x, float y, float dir_x, float dir_y, float progress, float range) {
    ecs::AttackEffect effect;
    effect.effect_type = "orbit";
    effect.x = x;
    effect.y = y;
    effect.direction_x = dir_x;
    effect.direction_y = dir_y;
    effect.duration = 0.6f;
    effect.timer = effect.duration * (1.0f - progress);
    effect.range = range;
    draw_attack_effect(effect);
}

void Renderer::draw_archer_arrow(float x, float y, float dir_x, float dir_y, float progress, float range) {
    ecs::AttackEffect effect;
    effect.effect_type = "arrow";
    effect.x = x;
    effect.y = y;
    effect.direction_x = dir_x;
    effect.direction_y = dir_y;
    effect.duration = 0.5f;
    effect.timer = effect.duration * (1.0f - progress);
    effect.range = range;
    draw_attack_effect(effect);
}

// ============================================================================
// UI RENDERING (delegates to UIRenderer)
// ============================================================================

void Renderer::begin_ui() {
    // Get command buffer for this frame
    SDL_GPUCommandBuffer* cmd = context_.current_command_buffer();
    if (!cmd) {
        std::cerr << "begin_ui: No active command buffer" << std::endl;
        return;
    }

    // Use swapchain texture acquired in begin_main_pass(), or acquire now if not available
    if (!current_swapchain_) {
        uint32_t sw_width, sw_height;
        current_swapchain_ = context_.acquire_swapchain_texture(cmd, &sw_width, &sw_height);
        if (!current_swapchain_) {
            // This can happen normally (minimized window, vsync timing) - skip this frame silently
            return;
        }
    }

    // Begin UI recording phase (no render pass yet - that's created in execute())
    ui_.begin(cmd);
}

void Renderer::end_ui() {
    // End UI recording phase
    ui_.end();

    // Execute UI rendering: upload data (copy pass) then render (render pass)
    // If no main 3D pass happened (menu state), clear the background
    SDL_GPUCommandBuffer* cmd = context_.current_command_buffer();
    if (cmd && current_swapchain_) {
        bool clear_background = !had_main_pass_this_frame_;
        ui_.execute(cmd, current_swapchain_, clear_background);
    }

    // UI render pass is managed by UIRenderer::execute() now
    ui_render_pass_ = nullptr;
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

// ============================================================================
// SCENE-BASED RENDERING API
// ============================================================================

void Renderer::render(const RenderScene& scene, const UIScene& ui_scene) {
    // Begin frame and acquire command buffer first
    begin_frame();

    // Only start main 3D render pass if there's 3D content to draw
    // For UI-only scenes (menus), skip the main pass entirely
    if (scene.has_3d_content()) {
        // Begin main 3D render pass (SDL3 GPU)
        begin_main_pass();

        SDL_GPUCommandBuffer* cmd = context_.current_command_buffer();

        // Draw world elements using SDL3 GPU renderers
        if (main_render_pass_ && cmd) {
            // Skybox (SDL3 GPU path)
            if (scene.should_draw_skybox() && skybox_enabled_) {
                skybox_time_ += 0.016f;
                world_.update(0.016f);
                world_.render_skybox(main_render_pass_, cmd, view_, projection_);
            }

            // Terrain (SDL3 GPU path)
            if (scene.should_draw_ground()) {
                terrain_.render(main_render_pass_, cmd, view_, projection_, actual_camera_pos_,
                               light_dir_);
            }

            // Grass (SDL3 GPU path)
            if (scene.should_draw_grass() && grass_enabled_ && grass_renderer_) {
                grass_renderer_->update(0.016f, skybox_time_);
                grass_renderer_->render(main_render_pass_, cmd, view_, projection_,
                                        actual_camera_pos_, light_dir_);
            }

            // Draw entities (SDL3 GPU path - models rendered inside main pass)
            for (const auto& entity_cmd : scene.entities()) {
                // Filter environment objects based on scene flags
                if (entity_cmd.state.type == EntityType::Environment) {
                    bool is_tree = (std::strncmp(entity_cmd.state.model_name, "tree_", 5) == 0);
                    if (is_tree && !scene.should_draw_trees()) continue;
                    if (!is_tree && !scene.should_draw_rocks()) continue;
                }
                draw_entity(entity_cmd.state, entity_cmd.is_local);
            }

            // Draw attack effects (SDL3 GPU path)
            for (const auto& effect_cmd : scene.effects()) {
                draw_attack_effect(effect_cmd.effect);
            }

            // Mountains (draws models which need main_render_pass_)
            if (scene.should_draw_mountains()) {
                draw_distant_mountains();
            }
        }

        // End main 3D render pass
        end_main_pass();
    }

    // Draw UI from scene (SDL3 GPU)
    begin_ui();
    render_ui(ui_scene);
    end_ui();

    end_frame();
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
