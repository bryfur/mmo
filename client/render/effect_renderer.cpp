#include "effect_renderer.hpp"
#include "../gpu/gpu_texture.hpp"
#include <cmath>
#include <iostream>
#include <glm/gtc/matrix_transform.hpp>

namespace mmo {

// Uniform structures for shader data (must match HLSL shaders in pipeline_registry.cpp)

// Model vertex shader uniforms (b0): matches pipeline_registry model_vertex
struct alignas(16) EffectVertexUniforms {
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 projection;
    glm::vec3 camera_pos;
    float _padding0;
    glm::mat4 light_space_matrix;
};

// Model fragment shader uniforms (b0): matches pipeline_registry model_fragment
struct alignas(16) EffectFragmentUniforms {
    glm::vec3 light_dir;
    float ambient;
    glm::vec3 light_color;
    float _padding1;
    glm::vec4 tint_color;
    glm::vec3 fog_color;
    float fog_start;
    float fog_end;
    int has_texture;
    int shadows_enabled;
    int fog_enabled;
};

EffectRenderer::EffectRenderer() = default;

EffectRenderer::~EffectRenderer() {
    shutdown();
}

bool EffectRenderer::init(gpu::GPUDevice& device, gpu::PipelineRegistry& pipeline_registry,
                          ModelManager* model_manager) {
    device_ = &device;
    pipeline_registry_ = &pipeline_registry;
    model_manager_ = model_manager;
    
    // Create sampler for effect textures
    SDL_GPUSamplerCreateInfo sampler_info = {};
    sampler_info.min_filter = SDL_GPU_FILTER_LINEAR;
    sampler_info.mag_filter = SDL_GPU_FILTER_LINEAR;
    sampler_info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
    sampler_info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    sampler_info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    sampler_info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    sampler_info.max_anisotropy = 4.0f;
    sampler_info.enable_anisotropy = true;
    sampler_ = SDL_CreateGPUSampler(device_->handle(), &sampler_info);
    if (!sampler_) {
        std::cerr << "EffectRenderer: Failed to create sampler: " << SDL_GetError() << std::endl;
        return false;
    }
    
    return true;
}

void EffectRenderer::shutdown() {
    if (sampler_ && device_) {
        SDL_ReleaseGPUSampler(device_->handle(), sampler_);
        sampler_ = nullptr;
    }
    
    device_ = nullptr;
    pipeline_registry_ = nullptr;
    model_manager_ = nullptr;
}

float EffectRenderer::get_terrain_height(float x, float z) const {
    if (terrain_height_func_) {
        return terrain_height_func_(x, z);
    }
    return 0.0f;
}

void EffectRenderer::draw_attack_effect(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd,
                                         const ecs::AttackEffect& effect,
                                         const glm::mat4& view, const glm::mat4& projection,
                                         const glm::vec3& camera_pos) {
    if (!pass || !cmd || !device_ || !pipeline_registry_) return;
    
    float progress = 1.0f - (effect.timer / effect.duration);
    progress = std::max(0.0f, std::min(1.0f, progress));
    
    switch (effect.attacker_class) {
        case PlayerClass::Warrior:
            draw_warrior_slash(pass, cmd, effect.x, effect.y, effect.direction_x, effect.direction_y, 
                              progress, view, projection, camera_pos);
            break;
        case PlayerClass::Mage:
            draw_mage_beam(pass, cmd, effect.x, effect.y, effect.direction_x, effect.direction_y, 
                          progress, config::MAGE_ATTACK_RANGE, view, projection, camera_pos);
            break;
        case PlayerClass::Paladin:
            draw_paladin_aoe(pass, cmd, effect.x, effect.y, effect.direction_x, effect.direction_y, 
                            progress, config::PALADIN_ATTACK_RANGE, view, projection, camera_pos);
            break;
        case PlayerClass::Archer:
            draw_archer_arrow(pass, cmd, effect.x, effect.y, effect.direction_x, effect.direction_y, 
                             progress, config::ARCHER_ATTACK_RANGE, view, projection, camera_pos);
            break;
    }
}

void EffectRenderer::draw_model_effect(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd,
                                        Model* model, const glm::mat4& model_mat,
                                        const glm::mat4& view, const glm::mat4& projection,
                                        const glm::vec3& camera_pos,
                                        const glm::vec4& tint_color,
                                        const glm::vec3& light_color,
                                        const glm::vec3& ambient_color) {
    if (!model) return;
    
    // Ensure model has GPU buffers uploaded
    if (!model->meshes.empty() && !model->meshes[0].uploaded) {
        ModelLoader::upload_to_gpu(*device_, *model);
    }
    
    // Use model pipeline for effect meshes (SDL3 GPU migration stage)
    auto* pipeline = pipeline_registry_->get_pipeline(gpu::PipelineType::Model);
    if (!pipeline) {
        std::cerr << "EffectRenderer: Model pipeline not available" << std::endl;
        return;
    }
    pipeline->bind(pass);
    
    // Set up vertex uniforms
    EffectVertexUniforms vertex_uniforms;
    vertex_uniforms.model = model_mat;
    vertex_uniforms.view = view;
    vertex_uniforms.projection = projection;
    vertex_uniforms.camera_pos = camera_pos;
    vertex_uniforms._padding0 = 0.0f;
    vertex_uniforms.light_space_matrix = glm::mat4(1.0f); // No shadows for effects
    
    SDL_PushGPUVertexUniformData(cmd, 0, &vertex_uniforms, sizeof(vertex_uniforms));
    
    // Set up fragment uniforms
    EffectFragmentUniforms frag_uniforms;
    frag_uniforms.light_dir = glm::vec3(-0.3f, -1.0f, -0.5f);
    // Use a single channel for ambient intensity to match shader's float ambient
    frag_uniforms.ambient = ambient_color.r;
    frag_uniforms.light_color = light_color;
    frag_uniforms._padding1 = 0.0f;
    frag_uniforms.tint_color = tint_color;
    frag_uniforms.fog_color = glm::vec3(0.7f, 0.8f, 0.9f);
    frag_uniforms.fog_start = 500.0f;
    frag_uniforms.fog_end = 1500.0f;
    frag_uniforms.has_texture = 0;
    frag_uniforms.shadows_enabled = 0;
    frag_uniforms.fog_enabled = 0;
    
    // Draw each mesh
    for (auto& mesh : model->meshes) {
        if (!mesh.uploaded || !mesh.vertex_buffer || !mesh.index_buffer) {
            continue;
        }
        
        // Update has_texture based on mesh
        if (mesh.has_texture && mesh.texture) {
            frag_uniforms.has_texture = 1;
            
            // Bind texture
            SDL_GPUTextureSamplerBinding tex_binding;
            tex_binding.texture = mesh.texture->handle();
            tex_binding.sampler = sampler_;
            SDL_BindGPUFragmentSamplers(pass, 0, &tex_binding, 1);
        } else {
            frag_uniforms.has_texture = 0;
        }
        
        SDL_PushGPUFragmentUniformData(cmd, 0, &frag_uniforms, sizeof(frag_uniforms));
        
        // Bind vertex buffer
        SDL_GPUBufferBinding vb_binding;
        vb_binding.buffer = mesh.vertex_buffer->handle();
        vb_binding.offset = 0;
        SDL_BindGPUVertexBuffers(pass, 0, &vb_binding, 1);
        
        // Bind index buffer
        SDL_GPUBufferBinding ib_binding;
        ib_binding.buffer = mesh.index_buffer->handle();
        ib_binding.offset = 0;
        SDL_BindGPUIndexBuffer(pass, &ib_binding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
        
        // Draw
        SDL_DrawGPUIndexedPrimitives(pass, static_cast<uint32_t>(mesh.indices.size()), 1, 0, 0, 0);
    }
}

void EffectRenderer::draw_warrior_slash(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd,
                                         float x, float y, float dir_x, float dir_y, float progress,
                                         const glm::mat4& view, const glm::mat4& projection,
                                         const glm::vec3& camera_pos) {
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
    
    draw_model_effect(pass, cmd, sword, model_mat, view, projection, camera_pos,
                      glm::vec4(1.0f, 1.0f, 1.0f, alpha),
                      glm::vec3(1.0f, 0.95f, 0.9f),
                      glm::vec3(0.4f, 0.4f, 0.5f));
}

void EffectRenderer::draw_mage_beam(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd,
                                     float x, float y, float dir_x, float dir_y, float progress, float range,
                                     const glm::mat4& view, const glm::mat4& projection,
                                     const glm::vec3& camera_pos) {
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
    
    draw_model_effect(pass, cmd, fireball, model_mat, view, projection, camera_pos,
                      glm::vec4(1.0f, 0.8f, 0.5f, alpha),
                      glm::vec3(1.5f, 1.2f, 0.8f),
                      glm::vec3(0.6f, 0.4f, 0.2f));
}

void EffectRenderer::draw_paladin_aoe(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd,
                                       float x, float y, float dir_x, float dir_y, float progress, float range,
                                       const glm::mat4& view, const glm::mat4& projection,
                                       const glm::vec3& camera_pos) {
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
        
        draw_model_effect(pass, cmd, bible, model_mat, view, projection, camera_pos,
                          glm::vec4(1.0f, 1.0f, 0.8f, alpha),
                          glm::vec3(1.2f, 1.2f, 0.8f),
                          glm::vec3(0.5f, 0.5f, 0.3f));
    }
}

void EffectRenderer::draw_archer_arrow(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd,
                                        float x, float y, float dir_x, float dir_y, float progress, float range,
                                        const glm::mat4& view, const glm::mat4& projection,
                                        const glm::vec3& camera_pos) {
    if (!model_manager_) return;
    
    float travel_dist = progress * range;
    float arrow_x = x + dir_x * travel_dist;
    float arrow_z = y + dir_y * travel_dist;
    float terrain_y = get_terrain_height(arrow_x, arrow_z);
    float arc_height = 30.0f * std::sin(progress * 3.14159f);
    float arrow_y = terrain_y + 30.0f + arc_height;
    
    float angle = std::atan2(dir_x, dir_y);
    float alpha = progress > 0.9f ? (1.0f - progress) * 10.0f : 1.0f;
    
    glm::mat4 model_mat = glm::mat4(1.0f);
    model_mat = glm::translate(model_mat, glm::vec3(arrow_x, arrow_y, arrow_z));
    model_mat = glm::rotate(model_mat, angle, glm::vec3(0.0f, 1.0f, 0.0f));
    float tilt = (progress - 0.5f) * 0.3f;
    model_mat = glm::rotate(model_mat, tilt, glm::vec3(1.0f, 0.0f, 0.0f));
    model_mat = glm::scale(model_mat, glm::vec3(1.5f, 1.5f, 12.0f));
    
    // Use fireball model as projectile stand-in
    Model* projectile = model_manager_->get_model("spell_fireball");
    if (projectile) {
        draw_model_effect(pass, cmd, projectile, model_mat, view, projection, camera_pos,
                          glm::vec4(0.6f, 0.4f, 0.2f, alpha),
                          glm::vec3(0.9f, 0.85f, 0.7f),
                          glm::vec3(0.4f, 0.35f, 0.3f));
    }
}

} // namespace mmo
