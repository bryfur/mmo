#include "effect_renderer.hpp"
#include <cmath>
#include <iostream>

namespace mmo {

EffectRenderer::EffectRenderer() = default;

EffectRenderer::~EffectRenderer() {
    shutdown();
}

bool EffectRenderer::init(ModelManager* model_manager) {
    model_manager_ = model_manager;
    
    model_shader_ = std::make_unique<Shader>();
    if (!model_shader_->load(shaders::model_vertex, shaders::model_fragment)) {
        std::cerr << "Failed to load effect model shader" << std::endl;
        return false;
    }
    
    return true;
}

void EffectRenderer::shutdown() {
    model_shader_.reset();
}

float EffectRenderer::get_terrain_height(float x, float z) const {
    if (terrain_height_func_) {
        return terrain_height_func_(x, z);
    }
    return 0.0f;
}

void EffectRenderer::draw_attack_effect(const ecs::AttackEffect& effect,
                                         const glm::mat4& view, const glm::mat4& projection) {
    float progress = 1.0f - (effect.timer / effect.duration);
    progress = std::max(0.0f, std::min(1.0f, progress));
    
    switch (effect.attacker_class) {
        case PlayerClass::Warrior:
            draw_warrior_slash(effect.x, effect.y, effect.direction_x, effect.direction_y, 
                              progress, view, projection);
            break;
        case PlayerClass::Mage:
            draw_mage_beam(effect.x, effect.y, effect.direction_x, effect.direction_y, 
                          progress, config::MAGE_ATTACK_RANGE, view, projection);
            break;
        case PlayerClass::Paladin:
            draw_paladin_aoe(effect.x, effect.y, effect.direction_x, effect.direction_y, 
                            progress, config::PALADIN_ATTACK_RANGE, view, projection);
            break;
        case PlayerClass::Archer:
            draw_archer_arrow(effect.x, effect.y, effect.direction_x, effect.direction_y, 
                             progress, config::ARCHER_ATTACK_RANGE, view, projection);
            break;
    }
}

void EffectRenderer::draw_warrior_slash(float x, float y, float dir_x, float dir_y, float progress,
                                         const glm::mat4& view, const glm::mat4& projection) {
    if (!model_manager_) return;
    
    Model* sword = model_manager_->get_model("weapon_sword");
    if (!sword) return;
    
    float base_angle = std::atan2(dir_x, dir_y);
    float swing_angle = -1.0f + progress * 2.0f;
    float rotation = base_angle + swing_angle;
    
    float swing_radius = config::WARRIOR_ATTACK_RANGE * 0.6f;
    float pos_x = x + std::sin(rotation) * swing_radius;
    float pos_z = y + std::cos(rotation) * swing_radius;
    float terrain_y = get_terrain_height(pos_x, pos_z);
    float pos_y = terrain_y + 25.0f + std::sin(progress * 3.14159f) * 15.0f;
    
    float tilt = std::sin(progress * 3.14159f) * 0.8f;
    float scale = 25.0f / sword->max_dimension();
    float alpha = progress < 0.7f ? 1.0f : (1.0f - (progress - 0.7f) / 0.3f);
    
    model_shader_->use();
    glm::mat4 model_mat = glm::mat4(1.0f);
    model_mat = glm::translate(model_mat, glm::vec3(pos_x, pos_y, pos_z));
    model_mat = glm::rotate(model_mat, rotation + 1.57f, glm::vec3(0.0f, 1.0f, 0.0f));
    model_mat = glm::rotate(model_mat, tilt, glm::vec3(1.0f, 0.0f, 0.0f));
    model_mat = glm::rotate(model_mat, -0.5f, glm::vec3(0.0f, 0.0f, 1.0f));
    model_mat = glm::scale(model_mat, glm::vec3(scale));
    
    float cx = (sword->min_x + sword->max_x) / 2.0f;
    float cy = sword->min_y;
    float cz = (sword->min_z + sword->max_z) / 2.0f;
    model_mat = model_mat * glm::translate(glm::mat4(1.0f), glm::vec3(-cx, -cy, -cz));
    
    model_shader_->set_mat4("model", model_mat);
    model_shader_->set_mat4("view", view);
    model_shader_->set_mat4("projection", projection);
    model_shader_->set_vec3("lightDir", glm::vec3(-0.3f, -1.0f, -0.5f));
    model_shader_->set_vec3("lightColor", glm::vec3(1.0f, 0.95f, 0.9f));
    model_shader_->set_vec3("ambientColor", glm::vec3(0.4f, 0.4f, 0.5f));
    model_shader_->set_vec4("tintColor", glm::vec4(1.0f, 1.0f, 1.0f, alpha));
    model_shader_->set_int("fogEnabled", 0);
    model_shader_->set_int("shadowsEnabled", 0);
    model_shader_->set_int("ssaoEnabled", 0);
    
    for (auto& mesh : sword->meshes) {
        if (!mesh.uploaded) ModelLoader::upload_to_gpu_legacy(*sword);
        if (mesh.vao && !mesh.indices.empty()) {
            if (mesh.has_texture && mesh.texture_id > 0) {
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, mesh.texture_id);
                model_shader_->set_int("baseColorTexture", 0);
                model_shader_->set_int("hasTexture", 1);
            } else {
                model_shader_->set_int("hasTexture", 0);
                model_shader_->set_vec4("baseColor", glm::vec4(0.8f, 0.8f, 0.9f, alpha));
            }
            glBindVertexArray(mesh.vao);
            glDrawElements(GL_TRIANGLES, mesh.indices.size(), GL_UNSIGNED_INT, nullptr);
            glBindVertexArray(0);
        }
    }
}

void EffectRenderer::draw_mage_beam(float x, float y, float dir_x, float dir_y, float progress, float range,
                                     const glm::mat4& view, const glm::mat4& projection) {
    if (!model_manager_) return;
    
    Model* fireball = model_manager_->get_model("spell_fireball");
    if (!fireball) return;
    
    float len = std::sqrt(dir_x * dir_x + dir_y * dir_y);
    if (len < 0.001f) { dir_x = 0; dir_y = 1; len = 1; }
    dir_x /= len;
    dir_y /= len;
    
    float travel_dist = range * progress;
    float pos_x = x + dir_x * travel_dist;
    float pos_z = y + dir_y * travel_dist;
    float terrain_y = get_terrain_height(pos_x, pos_z);
    float pos_y = terrain_y + 30.0f + std::sin(progress * 6.28f) * 5.0f;
    
    float spin = progress * 10.0f;
    float scale = 15.0f / fireball->max_dimension();
    float size_mod = progress < 0.2f ? progress / 0.2f : 1.0f;
    float alpha = progress > 0.8f ? (1.0f - (progress - 0.8f) / 0.2f) : 1.0f;
    
    model_shader_->use();
    glm::mat4 model_mat = glm::mat4(1.0f);
    model_mat = glm::translate(model_mat, glm::vec3(pos_x, pos_y, pos_z));
    model_mat = glm::rotate(model_mat, spin, glm::vec3(0.0f, 1.0f, 0.0f));
    model_mat = glm::rotate(model_mat, spin * 0.7f, glm::vec3(1.0f, 0.0f, 0.0f));
    model_mat = glm::scale(model_mat, glm::vec3(scale * size_mod));
    
    float cx = (fireball->min_x + fireball->max_x) / 2.0f;
    float cy = (fireball->min_y + fireball->max_y) / 2.0f;
    float cz = (fireball->min_z + fireball->max_z) / 2.0f;
    model_mat = model_mat * glm::translate(glm::mat4(1.0f), glm::vec3(-cx, -cy, -cz));
    
    model_shader_->set_mat4("model", model_mat);
    model_shader_->set_mat4("view", view);
    model_shader_->set_mat4("projection", projection);
    model_shader_->set_vec3("lightDir", glm::vec3(-0.3f, -1.0f, -0.5f));
    model_shader_->set_vec3("lightColor", glm::vec3(1.5f, 1.2f, 0.8f));
    model_shader_->set_vec3("ambientColor", glm::vec3(0.6f, 0.4f, 0.2f));
    model_shader_->set_vec4("tintColor", glm::vec4(1.0f, 0.8f, 0.5f, alpha));
    model_shader_->set_int("fogEnabled", 0);
    model_shader_->set_int("shadowsEnabled", 0);
    model_shader_->set_int("ssaoEnabled", 0);
    
    glDisable(GL_CULL_FACE);
    for (auto& mesh : fireball->meshes) {
        if (!mesh.uploaded) ModelLoader::upload_to_gpu_legacy(*fireball);
        if (mesh.vao && !mesh.indices.empty()) {
            if (mesh.has_texture && mesh.texture_id > 0) {
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, mesh.texture_id);
                model_shader_->set_int("baseColorTexture", 0);
                model_shader_->set_int("hasTexture", 1);
            } else {
                model_shader_->set_int("hasTexture", 0);
                model_shader_->set_vec4("baseColor", glm::vec4(1.0f, 0.5f, 0.1f, alpha));
            }
            glBindVertexArray(mesh.vao);
            glDrawElements(GL_TRIANGLES, mesh.indices.size(), GL_UNSIGNED_INT, nullptr);
            glBindVertexArray(0);
        }
    }
    glEnable(GL_CULL_FACE);
}

void EffectRenderer::draw_paladin_aoe(float x, float y, float dir_x, float dir_y, float progress, float range,
                                       const glm::mat4& view, const glm::mat4& projection) {
    if (!model_manager_) return;
    
    Model* bible = model_manager_->get_model("spell_bible");
    if (!bible) return;
    
    int num_bibles = 3;
    float spin_speed = progress * 15.0f;
    float orbit_radius = range * 0.4f * std::min(1.0f, progress * 2.0f);
    float terrain_y = get_terrain_height(x, y);
    float base_height = terrain_y + 35.0f + std::sin(progress * 3.14159f) * 20.0f;
    
    float scale = 12.0f / bible->max_dimension();
    float alpha = progress > 0.7f ? (1.0f - (progress - 0.7f) / 0.3f) : 1.0f;
    
    model_shader_->use();
    model_shader_->set_vec3("lightDir", glm::vec3(-0.3f, -1.0f, -0.5f));
    model_shader_->set_vec3("lightColor", glm::vec3(1.2f, 1.2f, 0.8f));
    model_shader_->set_vec3("ambientColor", glm::vec3(0.5f, 0.5f, 0.3f));
    model_shader_->set_vec4("tintColor", glm::vec4(1.0f, 1.0f, 0.8f, alpha));
    model_shader_->set_int("fogEnabled", 0);
    model_shader_->set_int("shadowsEnabled", 0);
    model_shader_->set_int("ssaoEnabled", 0);
    
    glDisable(GL_CULL_FACE);
    for (int i = 0; i < num_bibles; ++i) {
        float angle = spin_speed + (i * 2.0f * 3.14159f / num_bibles);
        float pos_x = x + std::cos(angle) * orbit_radius;
        float pos_z = y + std::sin(angle) * orbit_radius;
        float pos_y = base_height + std::sin(angle * 2.0f) * 10.0f;
        
        glm::mat4 model_mat = glm::mat4(1.0f);
        model_mat = glm::translate(model_mat, glm::vec3(pos_x, pos_y, pos_z));
        model_mat = glm::rotate(model_mat, angle + 1.57f, glm::vec3(0.0f, 1.0f, 0.0f));
        model_mat = glm::rotate(model_mat, 0.3f, glm::vec3(1.0f, 0.0f, 0.0f));
        model_mat = glm::rotate(model_mat, spin_speed * 0.5f, glm::vec3(0.0f, 0.0f, 1.0f));
        model_mat = glm::scale(model_mat, glm::vec3(scale));
        
        float cx = (bible->min_x + bible->max_x) / 2.0f;
        float cy = (bible->min_y + bible->max_y) / 2.0f;
        float cz = (bible->min_z + bible->max_z) / 2.0f;
        model_mat = model_mat * glm::translate(glm::mat4(1.0f), glm::vec3(-cx, -cy, -cz));
        
        model_shader_->set_mat4("model", model_mat);
        model_shader_->set_mat4("view", view);
        model_shader_->set_mat4("projection", projection);
        
        for (auto& mesh : bible->meshes) {
            if (!mesh.uploaded) ModelLoader::upload_to_gpu_legacy(*bible);
            if (mesh.vao && !mesh.indices.empty()) {
                if (mesh.has_texture && mesh.texture_id > 0) {
                    glActiveTexture(GL_TEXTURE0);
                    glBindTexture(GL_TEXTURE_2D, mesh.texture_id);
                    model_shader_->set_int("baseColorTexture", 0);
                    model_shader_->set_int("hasTexture", 1);
                } else {
                    model_shader_->set_int("hasTexture", 0);
                    model_shader_->set_vec4("baseColor", glm::vec4(0.9f, 0.85f, 0.6f, alpha));
                }
                glBindVertexArray(mesh.vao);
                glDrawElements(GL_TRIANGLES, mesh.indices.size(), GL_UNSIGNED_INT, nullptr);
                glBindVertexArray(0);
            }
        }
    }
    glEnable(GL_CULL_FACE);
}

void EffectRenderer::draw_archer_arrow(float x, float y, float dir_x, float dir_y, float progress, float range,
                                        const glm::mat4& view, const glm::mat4& projection) {
    if (!model_manager_) return;
    
    float travel_dist = progress * range;
    float arrow_x = x + dir_x * travel_dist;
    float arrow_z = y + dir_y * travel_dist;
    float terrain_y = get_terrain_height(arrow_x, arrow_z);
    float arc_height = 30.0f * std::sin(progress * 3.14159f);
    float arrow_y = terrain_y + 30.0f + arc_height;
    
    float angle = std::atan2(dir_x, dir_y);
    float alpha = progress > 0.9f ? (1.0f - progress) * 10.0f : 1.0f;
    
    model_shader_->use();
    model_shader_->set_vec3("lightDir", glm::vec3(-0.3f, -1.0f, -0.5f));
    model_shader_->set_vec3("lightColor", glm::vec3(0.9f, 0.85f, 0.7f));
    model_shader_->set_vec3("ambientColor", glm::vec3(0.4f, 0.35f, 0.3f));
    model_shader_->set_vec4("tintColor", glm::vec4(0.6f, 0.4f, 0.2f, alpha));
    model_shader_->set_int("fogEnabled", 0);
    model_shader_->set_int("shadowsEnabled", 0);
    model_shader_->set_int("ssaoEnabled", 0);
    
    glm::mat4 model_mat = glm::mat4(1.0f);
    model_mat = glm::translate(model_mat, glm::vec3(arrow_x, arrow_y, arrow_z));
    model_mat = glm::rotate(model_mat, angle, glm::vec3(0.0f, 1.0f, 0.0f));
    float tilt = (progress - 0.5f) * 0.3f;
    model_mat = glm::rotate(model_mat, tilt, glm::vec3(1.0f, 0.0f, 0.0f));
    model_mat = glm::scale(model_mat, glm::vec3(1.5f, 1.5f, 12.0f));
    
    model_shader_->set_mat4("model", model_mat);
    model_shader_->set_mat4("view", view);
    model_shader_->set_mat4("projection", projection);
    model_shader_->set_int("hasTexture", 0);
    model_shader_->set_vec4("baseColor", glm::vec4(0.5f, 0.35f, 0.2f, alpha));
    
    Model* projectile = model_manager_->get_model("spell_fireball");
    if (projectile) {
        glDisable(GL_CULL_FACE);
        for (auto& mesh : projectile->meshes) {
            if (!mesh.uploaded) ModelLoader::upload_to_gpu_legacy(*projectile);
            if (mesh.vao && !mesh.indices.empty()) {
                glBindVertexArray(mesh.vao);
                glDrawElements(GL_TRIANGLES, mesh.indices.size(), GL_UNSIGNED_INT, nullptr);
                glBindVertexArray(0);
            }
        }
        glEnable(GL_CULL_FACE);
    }
}

} // namespace mmo
