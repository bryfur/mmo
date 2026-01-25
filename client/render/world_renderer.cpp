#include "world_renderer.hpp"
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace mmo {

WorldRenderer::WorldRenderer() = default;

WorldRenderer::~WorldRenderer() {
    shutdown();
}

bool WorldRenderer::init(float world_width, float world_height, ModelManager* model_manager) {
    world_width_ = world_width;
    world_height_ = world_height;
    model_manager_ = model_manager;
    
    // Create shaders
    skybox_shader_ = std::make_unique<Shader>();
    if (!skybox_shader_->load(shaders::skybox_vertex, shaders::skybox_fragment)) {
        std::cerr << "Failed to load skybox shader" << std::endl;
        return false;
    }
    
    grid_shader_ = std::make_unique<Shader>();
    if (!grid_shader_->load(shaders::grid_vertex, shaders::grid_fragment)) {
        std::cerr << "Failed to load grid shader" << std::endl;
        return false;
    }
    
    model_shader_ = std::make_unique<Shader>();
    if (!model_shader_->load(shaders::model_vertex, shaders::model_fragment)) {
        std::cerr << "Failed to load model shader" << std::endl;
        return false;
    }
    
    create_skybox_mesh();
    create_grid_mesh();
    generate_mountain_positions();
    
    return true;
}

void WorldRenderer::shutdown() {
    if (skybox_vao_) {
        glDeleteVertexArrays(1, &skybox_vao_);
        skybox_vao_ = 0;
    }
    if (skybox_vbo_) {
        glDeleteBuffers(1, &skybox_vbo_);
        skybox_vbo_ = 0;
    }
    if (grid_vao_) {
        glDeleteVertexArrays(1, &grid_vao_);
        grid_vao_ = 0;
    }
    if (grid_vbo_) {
        glDeleteBuffers(1, &grid_vbo_);
        grid_vbo_ = 0;
    }
    
    skybox_shader_.reset();
    grid_shader_.reset();
    model_shader_.reset();
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
    
    glGenVertexArrays(1, &skybox_vao_);
    glGenBuffers(1, &skybox_vbo_);
    
    glBindVertexArray(skybox_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, skybox_vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    
    glBindVertexArray(0);
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
    
    grid_vertex_count_ = grid_data.size() / 7;
    
    glGenVertexArrays(1, &grid_vao_);
    glGenBuffers(1, &grid_vbo_);
    
    glBindVertexArray(grid_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, grid_vbo_);
    glBufferData(GL_ARRAY_BUFFER, grid_data.size() * sizeof(float), 
                 grid_data.data(), GL_STATIC_DRAW);
    
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    
    glBindVertexArray(0);
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
}

void WorldRenderer::render_skybox(const glm::mat4& view, const glm::mat4& projection) {
    if (!skybox_shader_ || !skybox_vao_) return;
    
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    
    skybox_shader_->use();
    skybox_shader_->set_mat4("view", view);
    skybox_shader_->set_mat4("projection", projection);
    skybox_shader_->set_float("time", skybox_time_);
    skybox_shader_->set_vec3("sunDirection", sun_direction_);
    
    glBindVertexArray(skybox_vao_);
    glDrawArrays(GL_TRIANGLES, 0, 36);
    glBindVertexArray(0);
    
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE);
}

void WorldRenderer::render_mountains(const glm::mat4& view, const glm::mat4& projection,
                                      const glm::vec3& camera_pos, const glm::vec3& light_dir) {
    if (!model_manager_ || !model_shader_) return;
    
    Model* mountain_small = model_manager_->get_model("mountain_small");
    Model* mountain_medium = model_manager_->get_model("mountain_medium");
    Model* mountain_large = model_manager_->get_model("mountain_large");
    
    if (!mountain_small && !mountain_medium && !mountain_large) return;
    
    model_shader_->use();
    model_shader_->set_mat4("view", view);
    model_shader_->set_mat4("projection", projection);
    model_shader_->set_vec3("cameraPos", camera_pos);
    model_shader_->set_vec3("lightDir", light_dir);
    model_shader_->set_vec3("lightColor", glm::vec3(1.0f, 0.95f, 0.9f));
    model_shader_->set_vec3("ambientColor", glm::vec3(0.5f, 0.5f, 0.55f));
    model_shader_->set_vec4("tintColor", glm::vec4(1.0f));
    model_shader_->set_int("fogEnabled", 1);
    model_shader_->set_vec3("fogColor", glm::vec3(0.55f, 0.55f, 0.6f));
    model_shader_->set_float("fogStart", 3000.0f);
    model_shader_->set_float("fogEnd", 12000.0f);
    model_shader_->set_int("shadowsEnabled", 0);
    model_shader_->set_int("ssaoEnabled", 0);
    
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
        
        model_shader_->set_mat4("model", model_mat);
        
        for (auto& mesh : mountain->meshes) {
            if (!mesh.uploaded) ModelLoader::upload_to_gpu(*mountain);
            if (mesh.vao && !mesh.indices.empty()) {
                if (mesh.has_texture && mesh.texture_id > 0) {
                    glActiveTexture(GL_TEXTURE0);
                    glBindTexture(GL_TEXTURE_2D, mesh.texture_id);
                    model_shader_->set_int("baseColorTexture", 0);
                    model_shader_->set_int("hasTexture", 1);
                } else {
                    model_shader_->set_int("hasTexture", 0);
                }
                glBindVertexArray(mesh.vao);
                glDrawElements(GL_TRIANGLES, mesh.indices.size(), GL_UNSIGNED_INT, 0);
            }
        }
    }
    glBindVertexArray(0);
}

void WorldRenderer::render_grid(const glm::mat4& view, const glm::mat4& projection) {
    if (!grid_shader_ || !grid_vao_) return;
    
    grid_shader_->use();
    grid_shader_->set_mat4("view", view);
    grid_shader_->set_mat4("projection", projection);
    
    glBindVertexArray(grid_vao_);
    glDrawArrays(GL_LINES, 0, grid_vertex_count_);
    glBindVertexArray(0);
}

} // namespace mmo
