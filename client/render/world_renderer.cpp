#include "world_renderer.hpp"
#include "bgfx_utils.hpp"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace mmo {

WorldRenderer::WorldRenderer() = default;

WorldRenderer::~WorldRenderer() {
    shutdown();
}

bool WorldRenderer::init(float world_width, float world_height, ModelManager* model_manager) {
    world_width_ = world_width;
    world_height_ = world_height;
    model_manager_ = model_manager;
    
    // Initialize vertex layouts
    skybox_layout_
        .begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .end();
    
    grid_layout_
        .begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Float, true)
        .end();
    
    // Note: u_viewProj is a bgfx predefined uniform - use setViewTransform
    // Create only custom uniforms - skybox
    u_skybox_params_ = bgfx::createUniform("u_skyboxParams", bgfx::UniformType::Vec4);
    u_skybox_sunDir_ = bgfx::createUniform("u_sunDirection", bgfx::UniformType::Vec4);
    
    // Create uniforms - grid (uses predefined u_viewProj via setViewTransform)
    
    // Create uniforms - model rendering (u_viewProj/u_model are bgfx predefined)
    u_cameraPos_ = bgfx::createUniform("u_cameraPos", bgfx::UniformType::Vec4);
    u_lightDir_ = bgfx::createUniform("u_lightDir", bgfx::UniformType::Vec4);
    u_lightColor_ = bgfx::createUniform("u_lightColor", bgfx::UniformType::Vec4);
    u_ambientColor_ = bgfx::createUniform("u_ambientColor", bgfx::UniformType::Vec4);
    u_tintColor_ = bgfx::createUniform("u_tintColor", bgfx::UniformType::Vec4);
    u_fogParams_ = bgfx::createUniform("u_fogParams", bgfx::UniformType::Vec4);
    u_fogParams2_ = bgfx::createUniform("u_fogParams2", bgfx::UniformType::Vec4);
    u_lightSpaceMatrix_ = bgfx::createUniform("u_lightSpaceMatrix", bgfx::UniformType::Mat4);
    u_screenParams_ = bgfx::createUniform("u_screenParams", bgfx::UniformType::Vec4);
    
    // Create samplers
    s_baseColorTexture_ = bgfx::createUniform("s_baseColorTexture", bgfx::UniformType::Sampler);
    s_shadowMap_ = bgfx::createUniform("s_shadowMap", bgfx::UniformType::Sampler);
    s_ssaoTexture_ = bgfx::createUniform("s_ssaoTexture", bgfx::UniformType::Sampler);
    
    // Load shaders
    load_shaders();
    
    create_skybox_mesh();
    create_grid_mesh();
    generate_mountain_positions();
    
    return bgfx::isValid(skybox_program_);
}

void WorldRenderer::load_shaders() {
    skybox_program_ = bgfx_utils::load_program("skybox_vs", "skybox_fs");
    if (!bgfx::isValid(skybox_program_)) {
        std::cerr << "Failed to load skybox shaders" << std::endl;
    }
    
    // Grid uses a simple UI-style shader - we can use ui shaders or create grid shaders
    // For now, use ui shaders with position+color
    grid_program_ = bgfx_utils::load_program("ui_vs", "ui_fs");
    if (!bgfx::isValid(grid_program_)) {
        std::cerr << "Failed to load grid shaders" << std::endl;
    }
    
    model_program_ = bgfx_utils::load_program("model_vs", "model_fs");
    if (!bgfx::isValid(model_program_)) {
        std::cerr << "Failed to load model shaders" << std::endl;
    }
    
    std::cout << "WorldRenderer shaders loaded" << std::endl;
}

void WorldRenderer::shutdown() {
    // Destroy vertex buffers
    if (bgfx::isValid(skybox_vbh_)) {
        bgfx::destroy(skybox_vbh_);
        skybox_vbh_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(grid_vbh_)) {
        bgfx::destroy(grid_vbh_);
        grid_vbh_ = BGFX_INVALID_HANDLE;
    }
    
    // Destroy programs
    if (bgfx::isValid(skybox_program_)) {
        bgfx::destroy(skybox_program_);
        skybox_program_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(grid_program_)) {
        bgfx::destroy(grid_program_);
        grid_program_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(model_program_)) {
        bgfx::destroy(model_program_);
        model_program_ = BGFX_INVALID_HANDLE;
    }
    
    // Destroy uniforms - skybox (u_viewProj is bgfx predefined, not created by us)
    if (bgfx::isValid(u_skybox_params_)) bgfx::destroy(u_skybox_params_);
    if (bgfx::isValid(u_skybox_sunDir_)) bgfx::destroy(u_skybox_sunDir_);
    
    // Destroy uniforms - grid (u_viewProj is bgfx predefined)
    
    // Destroy uniforms - model (u_model and u_viewProj are bgfx predefined)
    if (bgfx::isValid(u_cameraPos_)) bgfx::destroy(u_cameraPos_);
    if (bgfx::isValid(u_lightDir_)) bgfx::destroy(u_lightDir_);
    if (bgfx::isValid(u_lightColor_)) bgfx::destroy(u_lightColor_);
    if (bgfx::isValid(u_ambientColor_)) bgfx::destroy(u_ambientColor_);
    if (bgfx::isValid(u_tintColor_)) bgfx::destroy(u_tintColor_);
    if (bgfx::isValid(u_fogParams_)) bgfx::destroy(u_fogParams_);
    if (bgfx::isValid(u_fogParams2_)) bgfx::destroy(u_fogParams2_);
    if (bgfx::isValid(u_lightSpaceMatrix_)) bgfx::destroy(u_lightSpaceMatrix_);
    if (bgfx::isValid(u_screenParams_)) bgfx::destroy(u_screenParams_);
    if (bgfx::isValid(s_baseColorTexture_)) bgfx::destroy(s_baseColorTexture_);
    if (bgfx::isValid(s_shadowMap_)) bgfx::destroy(s_shadowMap_);
    if (bgfx::isValid(s_ssaoTexture_)) bgfx::destroy(s_ssaoTexture_);
    
    // Reset handles (u_skybox_viewProj_, u_grid_viewProj_, u_model_, u_viewProj_ are bgfx predefined)
    u_skybox_params_ = BGFX_INVALID_HANDLE;
    u_skybox_sunDir_ = BGFX_INVALID_HANDLE;
    u_cameraPos_ = BGFX_INVALID_HANDLE;
    u_lightDir_ = BGFX_INVALID_HANDLE;
    u_lightColor_ = BGFX_INVALID_HANDLE;
    u_ambientColor_ = BGFX_INVALID_HANDLE;
    u_tintColor_ = BGFX_INVALID_HANDLE;
    u_fogParams_ = BGFX_INVALID_HANDLE;
    u_fogParams2_ = BGFX_INVALID_HANDLE;
    u_lightSpaceMatrix_ = BGFX_INVALID_HANDLE;
    u_screenParams_ = BGFX_INVALID_HANDLE;
    s_baseColorTexture_ = BGFX_INVALID_HANDLE;
    s_shadowMap_ = BGFX_INVALID_HANDLE;
    s_ssaoTexture_ = BGFX_INVALID_HANDLE;
}

void WorldRenderer::update(float dt) {
    skybox_time_ += dt;
}

float WorldRenderer::get_terrain_height(float x, float z) const {
    if (terrain_height_func_) {
        return terrain_height_func_(x, z);
    }
    return 0.0f;
}

void WorldRenderer::create_skybox_mesh() {
    float vertices[] = {
        // Back face
        -1.0f, -1.0f, -1.0f,  1.0f,  1.0f, -1.0f,  1.0f, -1.0f, -1.0f,
         1.0f,  1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f,  1.0f, -1.0f,
        // Front face
        -1.0f, -1.0f,  1.0f,  1.0f, -1.0f,  1.0f,  1.0f,  1.0f,  1.0f,
         1.0f,  1.0f,  1.0f, -1.0f,  1.0f,  1.0f, -1.0f, -1.0f,  1.0f,
        // Left face
        -1.0f,  1.0f,  1.0f, -1.0f,  1.0f, -1.0f, -1.0f, -1.0f, -1.0f,
        -1.0f, -1.0f, -1.0f, -1.0f, -1.0f,  1.0f, -1.0f,  1.0f,  1.0f,
        // Right face
         1.0f,  1.0f,  1.0f,  1.0f, -1.0f, -1.0f,  1.0f,  1.0f, -1.0f,
         1.0f, -1.0f, -1.0f,  1.0f,  1.0f,  1.0f,  1.0f, -1.0f,  1.0f,
        // Bottom face
        -1.0f, -1.0f, -1.0f,  1.0f, -1.0f, -1.0f,  1.0f, -1.0f,  1.0f,
         1.0f, -1.0f,  1.0f, -1.0f, -1.0f,  1.0f, -1.0f, -1.0f, -1.0f,
        // Top face
        -1.0f,  1.0f, -1.0f,  1.0f,  1.0f,  1.0f,  1.0f,  1.0f, -1.0f,
         1.0f,  1.0f,  1.0f, -1.0f,  1.0f, -1.0f, -1.0f,  1.0f,  1.0f,
    };
    
    const bgfx::Memory* mem = bgfx::copy(vertices, sizeof(vertices));
    skybox_vbh_ = bgfx::createVertexBuffer(mem, skybox_layout_);
}

void WorldRenderer::create_grid_mesh() {
    std::vector<float> grid_data;
    float grid_step = 100.0f;
    
    // Grid lines
    for (float x = 0; x <= world_width_; x += grid_step) {
        grid_data.insert(grid_data.end(), {x, 0.0f, 0.0f, 0.15f, 0.15f, 0.2f, 0.8f});
        grid_data.insert(grid_data.end(), {x, 0.0f, world_height_, 0.15f, 0.15f, 0.2f, 0.8f});
    }
    for (float z = 0; z <= world_height_; z += grid_step) {
        grid_data.insert(grid_data.end(), {0.0f, 0.0f, z, 0.15f, 0.15f, 0.2f, 0.8f});
        grid_data.insert(grid_data.end(), {world_width_, 0.0f, z, 0.15f, 0.15f, 0.2f, 0.8f});
    }
    
    // World boundary
    grid_data.insert(grid_data.end(), {0.0f, 0.0f, 0.0f, 0.4f, 0.4f, 0.5f, 1.0f});
    grid_data.insert(grid_data.end(), {world_width_, 0.0f, 0.0f, 0.4f, 0.4f, 0.5f, 1.0f});
    grid_data.insert(grid_data.end(), {world_width_, 0.0f, 0.0f, 0.4f, 0.4f, 0.5f, 1.0f});
    grid_data.insert(grid_data.end(), {world_width_, 0.0f, world_height_, 0.4f, 0.4f, 0.5f, 1.0f});
    grid_data.insert(grid_data.end(), {world_width_, 0.0f, world_height_, 0.4f, 0.4f, 0.5f, 1.0f});
    grid_data.insert(grid_data.end(), {0.0f, 0.0f, world_height_, 0.4f, 0.4f, 0.5f, 1.0f});
    grid_data.insert(grid_data.end(), {0.0f, 0.0f, world_height_, 0.4f, 0.4f, 0.5f, 1.0f});
    grid_data.insert(grid_data.end(), {0.0f, 0.0f, 0.0f, 0.4f, 0.4f, 0.5f, 1.0f});
    
    grid_vertex_count_ = static_cast<uint32_t>(grid_data.size() / 7);
    
    const bgfx::Memory* mem = bgfx::copy(grid_data.data(), static_cast<uint32_t>(grid_data.size() * sizeof(float)));
    grid_vbh_ = bgfx::createVertexBuffer(mem, grid_layout_);
}

void WorldRenderer::generate_mountain_positions() {
    mountain_positions_.clear();
    
    float world_center_x = world_width_ / 2.0f;
    float world_center_z = world_height_ / 2.0f;
    float ring_radius = 4000.0f;
    
    // EPIC MASSIVE mountains
    for (int ring = 0; ring < 2; ++ring) {
        float current_radius = ring_radius + ring * 3000.0f;
        int num_mountains = 8 + ring * 4;
        
        for (int i = 0; i < num_mountains; ++i) {
            float angle = (i / static_cast<float>(num_mountains)) * 2.0f * 3.14159f;
            float offset = std::sin(angle * 3.0f + ring) * 500.0f;
            float mx = world_center_x + std::cos(angle) * (current_radius + offset);
            float mz = world_center_z + std::sin(angle) * (current_radius + offset);
            
            MountainPosition mp;
            mp.x = mx;
            mp.z = mz;
            mp.rotation = angle * 57.2958f + std::sin(angle * 3.0f) * 45.0f;
            
            float base_scale = 4000.0f + ring * 2000.0f;
            mp.scale = base_scale + std::sin(angle * 4.0f + ring) * 1000.0f;
            mp.y = -mp.scale * 0.3f - 400.0f;
            mp.size_type = 2;
            
            mountain_positions_.push_back(mp);
        }
    }
    
    // TITAN peaks in the far distance
    for (int i = 0; i < 5; ++i) {
        float angle = (i / 5.0f) * 2.0f * 3.14159f + 0.3f;
        
        MountainPosition mp;
        mp.x = world_center_x + std::cos(angle) * 10000.0f;
        mp.z = world_center_z + std::sin(angle) * 10000.0f;
        mp.rotation = angle * 57.2958f + 45.0f;
        mp.scale = 8000.0f + std::sin(angle * 2.0f) * 1600.0f;
        mp.y = -mp.scale * 0.35f - 600.0f;
        mp.size_type = 2;
        
        mountain_positions_.push_back(mp);
    }
    
    generate_rock_positions();
}

void WorldRenderer::generate_rock_positions() {
    rock_positions_.clear();
    
    float world_center_x = world_width_ / 2.0f;
    float world_center_z = world_height_ / 2.0f;
    
    std::srand(12345);
    
    // Zone 1: Just outside playable area
    for (int i = 0; i < 40; ++i) {
        float angle = (std::rand() / static_cast<float>(RAND_MAX)) * 2.0f * 3.14159f;
        float dist = 800.0f + (std::rand() / static_cast<float>(RAND_MAX)) * 700.0f;
        
        RockPosition rp;
        rp.x = world_center_x + std::cos(angle) * dist;
        rp.z = world_center_z + std::sin(angle) * dist;
        rp.y = get_terrain_height(rp.x, rp.z);
        rp.rotation = (std::rand() / static_cast<float>(RAND_MAX)) * 360.0f;
        rp.scale = 15.0f + (std::rand() / static_cast<float>(RAND_MAX)) * 25.0f;
        rp.rock_type = std::rand() % 5;
        rock_positions_.push_back(rp);
    }
    
    // Zone 2: Mid distance
    for (int i = 0; i < 60; ++i) {
        float angle = (std::rand() / static_cast<float>(RAND_MAX)) * 2.0f * 3.14159f;
        float dist = 1500.0f + (std::rand() / static_cast<float>(RAND_MAX)) * 1000.0f;
        
        RockPosition rp;
        rp.x = world_center_x + std::cos(angle) * dist;
        rp.z = world_center_z + std::sin(angle) * dist;
        rp.y = get_terrain_height(rp.x, rp.z);
        rp.rotation = (std::rand() / static_cast<float>(RAND_MAX)) * 360.0f;
        rp.scale = 25.0f + (std::rand() / static_cast<float>(RAND_MAX)) * 40.0f;
        rp.rock_type = std::rand() % 5;
        rock_positions_.push_back(rp);
    }
    
    // Zone 3: Near mountains
    for (int i = 0; i < 50; ++i) {
        float angle = (std::rand() / static_cast<float>(RAND_MAX)) * 2.0f * 3.14159f;
        float dist = 2500.0f + (std::rand() / static_cast<float>(RAND_MAX)) * 1000.0f;
        
        RockPosition rp;
        rp.x = world_center_x + std::cos(angle) * dist;
        rp.z = world_center_z + std::sin(angle) * dist;
        rp.y = get_terrain_height(rp.x, rp.z);
        rp.rotation = (std::rand() / static_cast<float>(RAND_MAX)) * 360.0f;
        rp.scale = 40.0f + (std::rand() / static_cast<float>(RAND_MAX)) * 60.0f;
        rp.rock_type = std::rand() % 5;
        rock_positions_.push_back(rp);
    }
    
    generate_tree_positions();
}

void WorldRenderer::generate_tree_positions() {
    tree_positions_.clear();
    
    float world_center_x = world_width_ / 2.0f;
    float world_center_z = world_height_ / 2.0f;
    
    std::srand(67890);
    
    // Helper to check if a position is too close to existing trees
    auto is_too_close = [this](float x, float z, float min_dist) {
        float min_dist_sq = min_dist * min_dist;
        for (const auto& existing : tree_positions_) {
            float dx = x - existing.x;
            float dz = z - existing.z;
            if (dx * dx + dz * dz < min_dist_sq) {
                return true;
            }
        }
        return false;
    };
    
    // Minimum distance between trees (based on scale)
    const float base_min_dist = 150.0f;
    
    // Zone 1: Forest patches near playable area
    for (int i = 0; i < 30; ++i) {
        for (int attempt = 0; attempt < 10; ++attempt) {
            float angle = (std::rand() / static_cast<float>(RAND_MAX)) * 2.0f * 3.14159f;
            float dist = 400.0f + (std::rand() / static_cast<float>(RAND_MAX)) * 500.0f;
            
            float x = world_center_x + std::cos(angle) * dist;
            float z = world_center_z + std::sin(angle) * dist;
            
            if (!is_too_close(x, z, base_min_dist)) {
                TreePosition tp;
                tp.x = x;
                tp.z = z;
                tp.y = get_terrain_height(tp.x, tp.z);
                tp.rotation = (std::rand() / static_cast<float>(RAND_MAX)) * 360.0f;
                tp.scale = 240.0f + (std::rand() / static_cast<float>(RAND_MAX)) * 320.0f;
                tp.tree_type = std::rand() % 2;
                tree_positions_.push_back(tp);
                break;
            }
        }
    }
    
    // Zone 2: Scattered trees mid distance
    for (int i = 0; i < 50; ++i) {
        for (int attempt = 0; attempt < 10; ++attempt) {
            float angle = (std::rand() / static_cast<float>(RAND_MAX)) * 2.0f * 3.14159f;
            float dist = 900.0f + (std::rand() / static_cast<float>(RAND_MAX)) * 900.0f;
            
            float x = world_center_x + std::cos(angle) * dist;
            float z = world_center_z + std::sin(angle) * dist;
            
            if (!is_too_close(x, z, base_min_dist * 1.5f)) {
                TreePosition tp;
                tp.x = x;
                tp.z = z;
                tp.y = get_terrain_height(tp.x, tp.z);
                tp.rotation = (std::rand() / static_cast<float>(RAND_MAX)) * 360.0f;
                tp.scale = 320.0f + (std::rand() / static_cast<float>(RAND_MAX)) * 400.0f;
                tp.tree_type = std::rand() % 2;
                tree_positions_.push_back(tp);
                break;
            }
        }
    }
    
    // Zone 3: Sparse trees near mountains
    for (int i = 0; i < 25; ++i) {
        for (int attempt = 0; attempt < 10; ++attempt) {
            float angle = (std::rand() / static_cast<float>(RAND_MAX)) * 2.0f * 3.14159f;
            float dist = 1800.0f + (std::rand() / static_cast<float>(RAND_MAX)) * 1000.0f;
            
            float x = world_center_x + std::cos(angle) * dist;
            float z = world_center_z + std::sin(angle) * dist;
            
            if (!is_too_close(x, z, base_min_dist * 2.0f)) {
                TreePosition tp;
                tp.x = x;
                tp.z = z;
                tp.y = get_terrain_height(tp.x, tp.z);
                tp.rotation = (std::rand() / static_cast<float>(RAND_MAX)) * 360.0f;
                tp.scale = 400.0f + (std::rand() / static_cast<float>(RAND_MAX)) * 480.0f;
                tp.tree_type = std::rand() % 2;
                tree_positions_.push_back(tp);
                break;
            }
        }
    }
    
    // Clustered groves - larger spacing in groves
    for (int grove = 0; grove < 4; ++grove) {
        float grove_angle = grove * (2.0f * 3.14159f / 4.0f) + (std::rand() / static_cast<float>(RAND_MAX)) * 0.5f;
        float grove_dist = 600.0f + (std::rand() / static_cast<float>(RAND_MAX)) * 800.0f;
        float grove_x = world_center_x + std::cos(grove_angle) * grove_dist;
        float grove_z = world_center_z + std::sin(grove_angle) * grove_dist;
        
        int grove_size = 10 + std::rand() % 6;
        int grove_type = std::rand() % 2;
        
        for (int i = 0; i < grove_size; ++i) {
            for (int attempt = 0; attempt < 10; ++attempt) {
                float offset_angle = (std::rand() / static_cast<float>(RAND_MAX)) * 2.0f * 3.14159f;
                float offset_dist = 50.0f + (std::rand() / static_cast<float>(RAND_MAX)) * 150.0f;
                float x = grove_x + std::cos(offset_angle) * offset_dist;
                float z = grove_z + std::sin(offset_angle) * offset_dist;
                
                if (!is_too_close(x, z, base_min_dist)) {
                    TreePosition tp;
                    tp.x = x;
                    tp.z = z;
                    tp.y = get_terrain_height(tp.x, tp.z);
                    tp.rotation = (std::rand() / static_cast<float>(RAND_MAX)) * 360.0f;
                    tp.scale = 280.0f + (std::rand() / static_cast<float>(RAND_MAX)) * 280.0f;
                    tp.tree_type = (std::rand() % 10 < 7) ? grove_type : (1 - grove_type);
                    tree_positions_.push_back(tp);
                    break;
                }
            }
        }
    }
}

void WorldRenderer::render_skybox(bgfx::ViewId view_id, const glm::mat4& view, const glm::mat4& projection) {
    if (!bgfx::isValid(skybox_program_) || !bgfx::isValid(skybox_vbh_)) return;
    
    // Remove translation from view matrix for skybox
    glm::mat4 skybox_view = glm::mat4(glm::mat3(view));
    
    // Use bgfx's predefined u_viewProj via setViewTransform
    bgfx::setViewTransform(view_id, glm::value_ptr(skybox_view), glm::value_ptr(projection));
    
    float params[4] = { skybox_time_, 0.0f, 0.0f, 0.0f };
    bgfx::setUniform(u_skybox_params_, params);
    
    float sunDir[4] = { sun_direction_.x, sun_direction_.y, sun_direction_.z, 0.0f };
    bgfx::setUniform(u_skybox_sunDir_, sunDir);
    
    bgfx::setVertexBuffer(0, skybox_vbh_);
    
    // Skybox state: depth test <= (so skybox is at far plane), no depth write, no culling
    uint64_t state = BGFX_STATE_WRITE_RGB
                   | BGFX_STATE_WRITE_A
                   | BGFX_STATE_DEPTH_TEST_LEQUAL;
    bgfx::setState(state);
    
    bgfx::submit(view_id, skybox_program_);
}

void WorldRenderer::render_mountains(bgfx::ViewId view_id, const glm::mat4& view, const glm::mat4& projection,
                                      const glm::vec3& camera_pos, const glm::vec3& light_dir) {
    if (!model_manager_ || !bgfx::isValid(model_program_)) return;
    
    Model* mountain_small = model_manager_->get_model("mountain_small");
    Model* mountain_medium = model_manager_->get_model("mountain_medium");
    Model* mountain_large = model_manager_->get_model("mountain_large");
    
    if (!mountain_small && !mountain_medium && !mountain_large) return;
    
    // Use bgfx's predefined u_viewProj via setViewTransform
    bgfx::setViewTransform(view_id, glm::value_ptr(view), glm::value_ptr(projection));
    
    float camPos[4] = { camera_pos.x, camera_pos.y, camera_pos.z, 0.0f };
    bgfx::setUniform(u_cameraPos_, camPos);
    
    float lightDirVec[4] = { light_dir.x, light_dir.y, light_dir.z, 0.0f };
    bgfx::setUniform(u_lightDir_, lightDirVec);
    
    float lightColor[4] = { 1.0f, 0.95f, 0.9f, 1.0f };
    bgfx::setUniform(u_lightColor_, lightColor);
    
    float ambientColor[4] = { 0.5f, 0.5f, 0.55f, 1.0f };
    bgfx::setUniform(u_ambientColor_, ambientColor);
    
    float tintColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    bgfx::setUniform(u_tintColor_, tintColor);
    
    // Fog for mountains - extended range
    float fogParams[4] = { 0.55f, 0.55f, 0.6f, 3000.0f };  // fogColor.rgb, fogStart
    bgfx::setUniform(u_fogParams_, fogParams);
    
    float fogParams2[4] = { 12000.0f, 1.0f, 0.0f, 0.0f };  // fogEnd, fogEnabled, shadowsEnabled, ssaoEnabled
    bgfx::setUniform(u_fogParams2_, fogParams2);
    
    for (const auto& mp : mountain_positions_) {
        Model* mountain = nullptr;
        switch (mp.size_type) {
            case 0: mountain = mountain_small; break;
            case 1: mountain = mountain_medium; break;
            case 2: mountain = mountain_large; break;
        }
        if (!mountain) {
            mountain = mountain_medium ? mountain_medium : (mountain_small ? mountain_small : mountain_large);
        }
        if (!mountain) continue;
        
        glm::mat4 model_mat = glm::mat4(1.0f);
        model_mat = glm::translate(model_mat, glm::vec3(mp.x, mp.y, mp.z));
        model_mat = glm::rotate(model_mat, glm::radians(mp.rotation), glm::vec3(0.0f, 1.0f, 0.0f));
        model_mat = glm::scale(model_mat, glm::vec3(mp.scale));
        
        float cx = (mountain->min_x + mountain->max_x) / 2.0f;
        float cy = mountain->min_y;
        float cz = (mountain->min_z + mountain->max_z) / 2.0f;
        model_mat = model_mat * glm::translate(glm::mat4(1.0f), glm::vec3(-cx, -cy, -cz));
        
        // Use bgfx's predefined u_model via setTransform
        bgfx::setTransform(glm::value_ptr(model_mat));
        
        for (auto& mesh : mountain->meshes) {
            if (!mesh.uploaded) ModelLoader::upload_to_gpu(*mountain);
            if (bgfx::isValid(mesh.vbh) && bgfx::isValid(mesh.ibh) && !mesh.indices.empty()) {
                if (mesh.has_texture && bgfx::isValid(mesh.texture)) {
                    bgfx::setTexture(0, s_baseColorTexture_, mesh.texture);
                }
                
                bgfx::setVertexBuffer(0, mesh.vbh);
                bgfx::setIndexBuffer(mesh.ibh);
                
                uint64_t state = BGFX_STATE_WRITE_RGB
                               | BGFX_STATE_WRITE_A
                               | BGFX_STATE_WRITE_Z
                               | BGFX_STATE_DEPTH_TEST_LESS
                               | BGFX_STATE_CULL_CCW;
                bgfx::setState(state);
                
                bgfx::submit(view_id, model_program_);
            }
        }
    }
}

void WorldRenderer::render_rocks(bgfx::ViewId view_id, const glm::mat4& view, const glm::mat4& projection,
                                  const glm::vec3& camera_pos, const glm::mat4& light_space_matrix,
                                  bgfx::TextureHandle shadow_map, bool shadows_enabled,
                                  bgfx::TextureHandle ssao_texture, bool ssao_enabled,
                                  const glm::vec3& light_dir, const glm::vec2& screen_size) {
    if (!model_manager_ || !bgfx::isValid(model_program_)) return;
    
    Model* rocks[5] = {
        model_manager_->get_model("rock_boulder"),
        model_manager_->get_model("rock_slate"),
        model_manager_->get_model("rock_spire"),
        model_manager_->get_model("rock_cluster"),
        model_manager_->get_model("rock_mossy")
    };
    
    bool any_rock = false;
    for (int i = 0; i < 5; ++i) {
        if (rocks[i]) any_rock = true;
    }
    if (!any_rock) return;
    
    // Use bgfx's predefined u_viewProj via setViewTransform
    bgfx::setViewTransform(view_id, glm::value_ptr(view), glm::value_ptr(projection));
    
    float camPos[4] = { camera_pos.x, camera_pos.y, camera_pos.z, 0.0f };
    bgfx::setUniform(u_cameraPos_, camPos);
    
    float lightDirVec[4] = { light_dir.x, light_dir.y, light_dir.z, 0.0f };
    bgfx::setUniform(u_lightDir_, lightDirVec);
    
    float lightColor[4] = { 1.0f, 0.95f, 0.9f, 1.0f };
    bgfx::setUniform(u_lightColor_, lightColor);
    
    float ambientColor[4] = { 0.4f, 0.4f, 0.5f, 1.0f };
    bgfx::setUniform(u_ambientColor_, ambientColor);
    
    float tintColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    bgfx::setUniform(u_tintColor_, tintColor);
    
    float fogParams[4] = { fog_color_.r, fog_color_.g, fog_color_.b, fog_start_ };
    bgfx::setUniform(u_fogParams_, fogParams);
    
    float fogParams2[4] = { fog_end_, 1.0f, shadows_enabled ? 1.0f : 0.0f, ssao_enabled ? 1.0f : 0.0f };
    bgfx::setUniform(u_fogParams2_, fogParams2);
    
    bgfx::setUniform(u_lightSpaceMatrix_, glm::value_ptr(light_space_matrix));
    
    float screenParams[4] = { screen_size.x, screen_size.y, 0.0f, 0.0f };
    bgfx::setUniform(u_screenParams_, screenParams);
    
    float cull_dist_sq = 4000.0f * 4000.0f;
    
    for (const auto& rp : rock_positions_) {
        float dx = rp.x - camera_pos.x;
        float dz = rp.z - camera_pos.z;
        if (dx * dx + dz * dz > cull_dist_sq) continue;
        
        Model* rock = rocks[rp.rock_type];
        if (!rock) {
            for (int i = 0; i < 5; ++i) {
                if (rocks[i]) { rock = rocks[i]; break; }
            }
        }
        if (!rock) continue;
        
        float terrain_y = get_terrain_height(rp.x, rp.z);
        float sink_depth = rp.scale * 0.2f;
        
        glm::mat4 model_mat = glm::mat4(1.0f);
        model_mat = glm::translate(model_mat, glm::vec3(rp.x, terrain_y - sink_depth, rp.z));
        model_mat = glm::rotate(model_mat, glm::radians(rp.rotation), glm::vec3(0.0f, 1.0f, 0.0f));
        model_mat = glm::scale(model_mat, glm::vec3(rp.scale));
        
        float cx = (rock->min_x + rock->max_x) / 2.0f;
        float cy = rock->min_y;
        float cz = (rock->min_z + rock->max_z) / 2.0f;
        model_mat = model_mat * glm::translate(glm::mat4(1.0f), glm::vec3(-cx, -cy, -cz));
        
        // Use bgfx's predefined u_model via setTransform
        bgfx::setTransform(glm::value_ptr(model_mat));
        
        for (auto& mesh : rock->meshes) {
            if (!mesh.uploaded) ModelLoader::upload_to_gpu(*rock);
            if (bgfx::isValid(mesh.vbh) && bgfx::isValid(mesh.ibh) && !mesh.indices.empty()) {
                if (mesh.has_texture && bgfx::isValid(mesh.texture)) {
                    bgfx::setTexture(0, s_baseColorTexture_, mesh.texture);
                }
                if (bgfx::isValid(shadow_map)) {
                    bgfx::setTexture(2, s_shadowMap_, shadow_map);
                }
                if (bgfx::isValid(ssao_texture)) {
                    bgfx::setTexture(3, s_ssaoTexture_, ssao_texture);
                }
                
                bgfx::setVertexBuffer(0, mesh.vbh);
                bgfx::setIndexBuffer(mesh.ibh);
                
                uint64_t state = BGFX_STATE_WRITE_RGB
                               | BGFX_STATE_WRITE_A
                               | BGFX_STATE_WRITE_Z
                               | BGFX_STATE_DEPTH_TEST_LESS
                               | BGFX_STATE_CULL_CCW;
                bgfx::setState(state);
                
                bgfx::submit(view_id, model_program_);
            }
        }
    }
}

void WorldRenderer::render_trees(bgfx::ViewId view_id, const glm::mat4& view, const glm::mat4& projection,
                                  const glm::vec3& camera_pos, const glm::mat4& light_space_matrix,
                                  bgfx::TextureHandle shadow_map, bool shadows_enabled,
                                  bgfx::TextureHandle ssao_texture, bool ssao_enabled,
                                  const glm::vec3& light_dir, const glm::vec2& screen_size) {
    if (!model_manager_ || !bgfx::isValid(model_program_)) return;
    
    Model* trees[3] = {
        model_manager_->get_model("tree_oak"),
        model_manager_->get_model("tree_pine"),
        model_manager_->get_model("tree_dead")
    };
    
    bool any_tree = false;
    for (int i = 0; i < 3; ++i) {
        if (trees[i]) any_tree = true;
    }
    if (!any_tree) return;
    
    // Use bgfx's predefined u_viewProj via setViewTransform
    bgfx::setViewTransform(view_id, glm::value_ptr(view), glm::value_ptr(projection));
    
    float camPos[4] = { camera_pos.x, camera_pos.y, camera_pos.z, 0.0f };
    bgfx::setUniform(u_cameraPos_, camPos);
    
    float lightDirVec[4] = { light_dir.x, light_dir.y, light_dir.z, 0.0f };
    bgfx::setUniform(u_lightDir_, lightDirVec);
    
    float lightColor[4] = { 1.0f, 0.95f, 0.9f, 1.0f };
    bgfx::setUniform(u_lightColor_, lightColor);
    
    float ambientColor[4] = { 0.4f, 0.4f, 0.5f, 1.0f };
    bgfx::setUniform(u_ambientColor_, ambientColor);
    
    float tintColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    bgfx::setUniform(u_tintColor_, tintColor);
    
    float fogParams[4] = { fog_color_.r, fog_color_.g, fog_color_.b, fog_start_ };
    bgfx::setUniform(u_fogParams_, fogParams);
    
    float fogParams2[4] = { fog_end_, 1.0f, shadows_enabled ? 1.0f : 0.0f, ssao_enabled ? 1.0f : 0.0f };
    bgfx::setUniform(u_fogParams2_, fogParams2);
    
    bgfx::setUniform(u_lightSpaceMatrix_, glm::value_ptr(light_space_matrix));
    
    float screenParams[4] = { screen_size.x, screen_size.y, 0.0f, 0.0f };
    bgfx::setUniform(u_screenParams_, screenParams);
    
    float cull_dist_sq = 3500.0f * 3500.0f;
    
    for (const auto& tp : tree_positions_) {
        float dx = tp.x - camera_pos.x;
        float dz = tp.z - camera_pos.z;
        if (dx * dx + dz * dz > cull_dist_sq) continue;
        
        Model* tree = trees[tp.tree_type];
        if (!tree) {
            for (int i = 0; i < 3; ++i) {
                if (trees[i]) { tree = trees[i]; break; }
            }
        }
        if (!tree) continue;
        
        float terrain_y = get_terrain_height(tp.x, tp.z);
        
        glm::mat4 model_mat = glm::mat4(1.0f);
        model_mat = glm::translate(model_mat, glm::vec3(tp.x, terrain_y, tp.z));
        model_mat = glm::rotate(model_mat, glm::radians(tp.rotation), glm::vec3(0.0f, 1.0f, 0.0f));
        model_mat = glm::scale(model_mat, glm::vec3(tp.scale));
        
        float cx = (tree->min_x + tree->max_x) / 2.0f;
        float cy = tree->min_y;
        float cz = (tree->min_z + tree->max_z) / 2.0f;
        model_mat = model_mat * glm::translate(glm::mat4(1.0f), glm::vec3(-cx, -cy, -cz));
        
        // Use bgfx's predefined u_model via setTransform
        bgfx::setTransform(glm::value_ptr(model_mat));
        
        for (auto& mesh : tree->meshes) {
            if (!mesh.uploaded) ModelLoader::upload_to_gpu(*tree);
            if (bgfx::isValid(mesh.vbh) && bgfx::isValid(mesh.ibh) && !mesh.indices.empty()) {
                if (mesh.has_texture && bgfx::isValid(mesh.texture)) {
                    bgfx::setTexture(0, s_baseColorTexture_, mesh.texture);
                }
                if (bgfx::isValid(shadow_map)) {
                    bgfx::setTexture(2, s_shadowMap_, shadow_map);
                }
                if (bgfx::isValid(ssao_texture)) {
                    bgfx::setTexture(3, s_ssaoTexture_, ssao_texture);
                }
                
                bgfx::setVertexBuffer(0, mesh.vbh);
                bgfx::setIndexBuffer(mesh.ibh);
                
                uint64_t state = BGFX_STATE_WRITE_RGB
                               | BGFX_STATE_WRITE_A
                               | BGFX_STATE_WRITE_Z
                               | BGFX_STATE_DEPTH_TEST_LESS
                               | BGFX_STATE_CULL_CCW;
                bgfx::setState(state);
                
                bgfx::submit(view_id, model_program_);
            }
        }
    }
}

void WorldRenderer::render_grid(bgfx::ViewId view_id, const glm::mat4& view, const glm::mat4& projection) {
    if (!bgfx::isValid(grid_program_) || !bgfx::isValid(grid_vbh_)) return;
    
    // Use bgfx's predefined u_viewProj via setViewTransform
    bgfx::setViewTransform(view_id, glm::value_ptr(view), glm::value_ptr(projection));
    
    bgfx::setVertexBuffer(0, grid_vbh_);
    
    uint64_t state = BGFX_STATE_WRITE_RGB
                   | BGFX_STATE_WRITE_A
                   | BGFX_STATE_PT_LINES
                   | BGFX_STATE_BLEND_ALPHA;
    bgfx::setState(state);
    
    bgfx::submit(view_id, grid_program_);
}

std::vector<WorldRenderer::TreePositionData> WorldRenderer::get_tree_positions_for_shadows() const {
    std::vector<TreePositionData> result;
    result.reserve(tree_positions_.size());
    for (const auto& tp : tree_positions_) {
        result.push_back({tp.x, tp.y, tp.z, tp.rotation, tp.scale, tp.tree_type});
    }
    return result;
}
} // namespace mmo
