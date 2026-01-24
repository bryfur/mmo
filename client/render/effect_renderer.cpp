#include "effect_renderer.hpp"
#include "bgfx_utils.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cmath>
#include <iostream>

namespace mmo {

EffectRenderer::EffectRenderer() = default;

EffectRenderer::~EffectRenderer() {
    shutdown();
}

bool EffectRenderer::init(ModelManager* model_manager) {
    model_manager_ = model_manager;
    
    // Load effect model shader
    model_program_ = bgfx_utils::load_program("model_vs", "model_fs");
    if (!bgfx::isValid(model_program_)) {
        std::cerr << "Failed to load effect model shader" << std::endl;
        return false;
    }
    
    // Note: u_model is a bgfx predefined uniform - use setTransform
    // Create only custom uniforms
    u_lightDir_ = bgfx::createUniform("u_lightDir", bgfx::UniformType::Vec4);
    u_lightColor_ = bgfx::createUniform("u_lightColor", bgfx::UniformType::Vec4);
    u_ambientColor_ = bgfx::createUniform("u_ambientColor", bgfx::UniformType::Vec4);
    u_tintColor_ = bgfx::createUniform("u_tintColor", bgfx::UniformType::Vec4);
    u_baseColor_ = bgfx::createUniform("u_baseColor", bgfx::UniformType::Vec4);
    u_params_ = bgfx::createUniform("u_params", bgfx::UniformType::Vec4);
    s_baseColorTexture_ = bgfx::createUniform("s_baseColorTexture", bgfx::UniformType::Sampler);
    
    std::cout << "EffectRenderer initialized (bgfx)" << std::endl;
    return true;
}

void EffectRenderer::shutdown() {
    if (bgfx::isValid(model_program_)) {
        bgfx::destroy(model_program_);
        model_program_ = BGFX_INVALID_HANDLE;
    }
    
    auto destroy_uniform = [](bgfx::UniformHandle& h) {
        if (bgfx::isValid(h)) {
            bgfx::destroy(h);
            h = BGFX_INVALID_HANDLE;
        }
    };
    
    // Note: u_model is bgfx predefined, so we don't destroy it
    destroy_uniform(u_lightDir_);
    destroy_uniform(u_lightColor_);
    destroy_uniform(u_ambientColor_);
    destroy_uniform(u_tintColor_);
    destroy_uniform(u_baseColor_);
    destroy_uniform(u_params_);
    destroy_uniform(s_baseColorTexture_);
}

float EffectRenderer::get_terrain_height(float x, float z) const {
    if (terrain_height_func_) {
        return terrain_height_func_(x, z);
    }
    return 0.0f;
}

void EffectRenderer::render_model(Model* model, const glm::mat4& model_mat, 
                                   const glm::vec4& tint_color, float alpha,
                                   bgfx::ViewId view_id) {
    if (!model) return;
    
    // Set model matrix using bgfx's predefined u_model
    bgfx::setTransform(glm::value_ptr(model_mat));
    
    // Set tint color
    float tint[4] = {tint_color.r, tint_color.g, tint_color.b, alpha};
    bgfx::setUniform(u_tintColor_, tint);
    
    for (auto& mesh : model->meshes) {
        // Ensure GPU resources are uploaded
        if (!mesh.uploaded) {
            ModelLoader::upload_to_gpu(*model);
        }
        
        if (!bgfx::isValid(mesh.vbh) || !bgfx::isValid(mesh.ibh)) continue;
        
        // Set params (fogEnabled=0, shadowsEnabled=0, ssaoEnabled=0, hasTexture)
        float params[4] = {0.0f, 0.0f, 0.0f, mesh.has_texture ? 1.0f : 0.0f};
        bgfx::setUniform(u_params_, params);
        
        if (mesh.has_texture && bgfx::isValid(mesh.texture)) {
            bgfx::setTexture(0, s_baseColorTexture_, mesh.texture);
        } else {
            // Use base color
            float r = ((mesh.base_color >> 0) & 0xFF) / 255.0f;
            float g = ((mesh.base_color >> 8) & 0xFF) / 255.0f;
            float b = ((mesh.base_color >> 16) & 0xFF) / 255.0f;
            float a = ((mesh.base_color >> 24) & 0xFF) / 255.0f;
            float baseColor[4] = {r, g, b, a * alpha};
            bgfx::setUniform(u_baseColor_, baseColor);
        }
        
        bgfx::setVertexBuffer(0, mesh.vbh);
        bgfx::setIndexBuffer(mesh.ibh);
        
        // State: depth test, alpha blending, no culling for effects
        uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z
                       | BGFX_STATE_DEPTH_TEST_LESS
                       | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA);
        bgfx::setState(state);
        
        bgfx::submit(view_id, model_program_);
    }
}

void EffectRenderer::draw_attack_effect(const ecs::AttackEffect& effect,
                                         bgfx::ViewId view_id,
                                         const glm::mat4& view, const glm::mat4& projection) {
    float progress = 1.0f - (effect.timer / effect.duration);
    progress = std::max(0.0f, std::min(1.0f, progress));
    
    switch (effect.attacker_class) {
        case PlayerClass::Warrior:
            draw_warrior_slash(effect.x, effect.y, effect.direction_x, effect.direction_y, 
                              progress, view_id, view, projection);
            break;
        case PlayerClass::Mage:
            draw_mage_beam(effect.x, effect.y, effect.direction_x, effect.direction_y, 
                          progress, config::MAGE_ATTACK_RANGE, view_id, view, projection);
            break;
        case PlayerClass::Paladin:
            draw_paladin_aoe(effect.x, effect.y, effect.direction_x, effect.direction_y, 
                            progress, config::PALADIN_ATTACK_RANGE, view_id, view, projection);
            break;
        case PlayerClass::Archer:
            draw_archer_arrow(effect.x, effect.y, effect.direction_x, effect.direction_y, 
                             progress, config::ARCHER_ATTACK_RANGE, view_id, view, projection);
            break;
    }
}

void EffectRenderer::draw_warrior_slash(float x, float y, float dir_x, float dir_y, float progress,
                                         bgfx::ViewId view_id,
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
    
    // Set lighting
    float lightDir[4] = {-0.3f, -1.0f, -0.5f, 0.0f};
    float lightColor[4] = {1.0f, 0.95f, 0.9f, 1.0f};
    float ambientColor[4] = {0.4f, 0.4f, 0.5f, 1.0f};
    bgfx::setUniform(u_lightDir_, lightDir);
    bgfx::setUniform(u_lightColor_, lightColor);
    bgfx::setUniform(u_ambientColor_, ambientColor);
    
    render_model(sword, model_mat, glm::vec4(1.0f, 1.0f, 1.0f, alpha), alpha, view_id);
}

void EffectRenderer::draw_mage_beam(float x, float y, float dir_x, float dir_y, float progress, float range,
                                     bgfx::ViewId view_id,
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
    
    glm::mat4 model_mat = glm::mat4(1.0f);
    model_mat = glm::translate(model_mat, glm::vec3(pos_x, pos_y, pos_z));
    model_mat = glm::rotate(model_mat, spin, glm::vec3(0.0f, 1.0f, 0.0f));
    model_mat = glm::rotate(model_mat, spin * 0.7f, glm::vec3(1.0f, 0.0f, 0.0f));
    model_mat = glm::scale(model_mat, glm::vec3(scale * size_mod));
    
    float cx = (fireball->min_x + fireball->max_x) / 2.0f;
    float cy = (fireball->min_y + fireball->max_y) / 2.0f;
    float cz = (fireball->min_z + fireball->max_z) / 2.0f;
    model_mat = model_mat * glm::translate(glm::mat4(1.0f), glm::vec3(-cx, -cy, -cz));
    
    float lightDir[4] = {-0.3f, -1.0f, -0.5f, 0.0f};
    float lightColor[4] = {1.5f, 1.2f, 0.8f, 1.0f};
    float ambientColor[4] = {0.6f, 0.4f, 0.2f, 1.0f};
    bgfx::setUniform(u_lightDir_, lightDir);
    bgfx::setUniform(u_lightColor_, lightColor);
    bgfx::setUniform(u_ambientColor_, ambientColor);
    
    render_model(fireball, model_mat, glm::vec4(1.0f, 0.8f, 0.5f, alpha), alpha, view_id);
}

void EffectRenderer::draw_paladin_aoe(float x, float y, float dir_x, float dir_y, float progress, float range,
                                       bgfx::ViewId view_id,
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
    
    float lightDir[4] = {-0.3f, -1.0f, -0.5f, 0.0f};
    float lightColor[4] = {1.2f, 1.2f, 0.8f, 1.0f};
    float ambientColor[4] = {0.5f, 0.5f, 0.3f, 1.0f};
    bgfx::setUniform(u_lightDir_, lightDir);
    bgfx::setUniform(u_lightColor_, lightColor);
    bgfx::setUniform(u_ambientColor_, ambientColor);
    
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
        
        render_model(bible, model_mat, glm::vec4(1.0f, 1.0f, 0.8f, alpha), alpha, view_id);
    }
}

void EffectRenderer::draw_archer_arrow(float x, float y, float dir_x, float dir_y, float progress, float range,
                                        bgfx::ViewId view_id,
                                        const glm::mat4& view, const glm::mat4& projection) {
    if (!model_manager_) return;
    
    Model* projectile = model_manager_->get_model("spell_fireball");
    if (!projectile) return;
    
    float travel_dist = progress * range;
    float arrow_x = x + dir_x * travel_dist;
    float arrow_z = y + dir_y * travel_dist;
    float terrain_y = get_terrain_height(arrow_x, arrow_z);
    float arc_height = 30.0f * std::sin(progress * 3.14159f);
    float arrow_y = terrain_y + 30.0f + arc_height;
    
    float angle = std::atan2(dir_x, dir_y);
    float alpha = progress > 0.9f ? (1.0f - progress) * 10.0f : 1.0f;
    
    float lightDir[4] = {-0.3f, -1.0f, -0.5f, 0.0f};
    float lightColor[4] = {0.9f, 0.85f, 0.7f, 1.0f};
    float ambientColor[4] = {0.4f, 0.35f, 0.3f, 1.0f};
    bgfx::setUniform(u_lightDir_, lightDir);
    bgfx::setUniform(u_lightColor_, lightColor);
    bgfx::setUniform(u_ambientColor_, ambientColor);
    
    glm::mat4 model_mat = glm::mat4(1.0f);
    model_mat = glm::translate(model_mat, glm::vec3(arrow_x, arrow_y, arrow_z));
    model_mat = glm::rotate(model_mat, angle, glm::vec3(0.0f, 1.0f, 0.0f));
    float tilt = (progress - 0.5f) * 0.3f;
    model_mat = glm::rotate(model_mat, tilt, glm::vec3(1.0f, 0.0f, 0.0f));
    model_mat = glm::scale(model_mat, glm::vec3(1.5f, 1.5f, 12.0f));
    
    render_model(projectile, model_mat, glm::vec4(0.6f, 0.4f, 0.2f, alpha), alpha, view_id);
}

} // namespace mmo
