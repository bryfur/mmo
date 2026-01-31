#include "effect_renderer.hpp"
#include "../gpu/gpu_texture.hpp"
#include "../gpu/gpu_uniforms.hpp"
#include "../render_constants.hpp"
#include "SDL3/SDL_error.h"
#include "SDL3/SDL_gpu.h"
#include "engine/effect_types.hpp"
#include "engine/gpu/gpu_device.hpp"
#include "engine/gpu/pipeline_registry.hpp"
#include "engine/model_loader.hpp"
#include "glm/ext/matrix_float4x4.hpp"
#include "glm/ext/matrix_transform.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>

namespace mmo::engine::render {

namespace gpu = mmo::engine::gpu;

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
                                         const engine::EffectInstance& effect,
                                         const glm::mat4& view, const glm::mat4& projection,
                                         const glm::vec3& camera_pos) {
    if (!pass || !cmd || !device_ || !pipeline_registry_) return;
    
    float progress = 1.0f - (effect.timer / effect.duration);
    progress = std::max(0.0f, std::min(1.0f, progress));
    
    const std::string& type = effect.effect_type;
    float range = effect.range;

    const auto& model = effect.model_name;

    if (type == "melee_swing") {
        draw_melee_slash(pass, cmd, model, effect.x, effect.y, effect.direction_x, effect.direction_y,
                         progress, view, projection, camera_pos);
    } else if (type == "projectile") {
        draw_projectile(pass, cmd, model, effect.x, effect.y, effect.direction_x, effect.direction_y,
                        progress, range, view, projection, camera_pos);
    } else if (type == "orbit") {
        draw_orbit_aoe(pass, cmd, model, effect.x, effect.y, effect.direction_x, effect.direction_y,
                       progress, range, view, projection, camera_pos);
    } else if (type == "arrow") {
        draw_arrow(pass, cmd, model, effect.x, effect.y, effect.direction_x, effect.direction_y,
                   progress, range, view, projection, camera_pos);
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
    gpu::ModelTransformUniforms vertex_uniforms = {};
    vertex_uniforms.model = model_mat;
    vertex_uniforms.view = view;
    vertex_uniforms.projection = projection;
    vertex_uniforms.cameraPos = camera_pos;
    vertex_uniforms.normalMatrix = glm::mat4(1.0f);

    SDL_PushGPUVertexUniformData(cmd, 0, &vertex_uniforms, sizeof(vertex_uniforms));

    // Set up fragment uniforms (fog disabled for effects)
    gpu::ModelLightingUniforms frag_uniforms = {};
    frag_uniforms.lightDir = glm::vec3(-0.3f, -1.0f, -0.5f);
    frag_uniforms.lightColor = light_color;
    frag_uniforms.ambientColor = ambient_color;
    frag_uniforms.tintColor = tint_color;
    frag_uniforms.fogColor = fog::COLOR;
    frag_uniforms.fogStart = fog::START;
    frag_uniforms.fogEnd = fog::END;
    frag_uniforms.hasTexture = 0;
    frag_uniforms.fogEnabled = 0;
    
    // Draw each mesh
    for (auto& mesh : model->meshes) {
        if (!mesh.uploaded || !mesh.vertex_buffer || !mesh.index_buffer) {
            continue;
        }
        
        // Update hasTexture based on mesh
        if (mesh.has_texture && mesh.texture) {
            frag_uniforms.hasTexture = 1;
            
            // Bind texture
            SDL_GPUTextureSamplerBinding tex_binding;
            tex_binding.texture = mesh.texture->handle();
            tex_binding.sampler = sampler_;
            SDL_BindGPUFragmentSamplers(pass, 0, &tex_binding, 1);
        } else {
            frag_uniforms.hasTexture = 0;
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

void EffectRenderer::draw_melee_slash(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd,
                                         const std::string& model_name,
                                         float x, float y, float dir_x, float dir_y, float progress,
                                         const glm::mat4& view, const glm::mat4& projection,
                                         const glm::vec3& camera_pos) {
    if (!model_manager_) return;

    Model* model = model_manager_->get_model(model_name);
    if (!model) return;
    
    float base_angle = std::atan2(dir_x, dir_y);
    float swing_angle = -1.0f + progress * 2.0f;
    float rotation = base_angle + swing_angle;
    
    float swing_radius = 36.0f;  // Visual swing radius (cosmetic only)
    float pos_x = x + std::sin(rotation) * swing_radius;
    float pos_z = y + std::cos(rotation) * swing_radius;
    float terrain_y = get_terrain_height(pos_x, pos_z);
    float pos_y = terrain_y + 25.0f + std::sin(progress * 3.14159f) * 15.0f;

    float tilt = std::sin(progress * 3.14159f) * 0.8f;
    float scale = 25.0f / model->max_dimension();
    float alpha = progress < 0.7f ? 1.0f : (1.0f - (progress - 0.7f) / 0.3f);

    glm::mat4 model_mat = glm::mat4(1.0f);
    model_mat = glm::translate(model_mat, glm::vec3(pos_x, pos_y, pos_z));
    model_mat = glm::rotate(model_mat, rotation + 1.57f, glm::vec3(0.0f, 1.0f, 0.0f));
    model_mat = glm::rotate(model_mat, tilt, glm::vec3(1.0f, 0.0f, 0.0f));
    model_mat = glm::rotate(model_mat, -0.5f, glm::vec3(0.0f, 0.0f, 1.0f));
    model_mat = glm::scale(model_mat, glm::vec3(scale));

    float cx = (model->min_x + model->max_x) / 2.0f;
    float cy = model->min_y;
    float cz = (model->min_z + model->max_z) / 2.0f;
    model_mat = model_mat * glm::translate(glm::mat4(1.0f), glm::vec3(-cx, -cy, -cz));

    draw_model_effect(pass, cmd, model, model_mat, view, projection, camera_pos,
                      glm::vec4(1.0f, 1.0f, 1.0f, alpha),
                      glm::vec3(1.0f, 0.95f, 0.9f),
                      glm::vec3(0.4f, 0.4f, 0.5f));
}

void EffectRenderer::draw_projectile(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd,
                                     const std::string& model_name,
                                     float x, float y, float dir_x, float dir_y, float progress, float range,
                                     const glm::mat4& view, const glm::mat4& projection,
                                     const glm::vec3& camera_pos) {
    if (!model_manager_) return;

    Model* model = model_manager_->get_model(model_name);
    if (!model) return;
    
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
    float scale = 15.0f / model->max_dimension();
    float size_mod = progress < 0.2f ? progress / 0.2f : 1.0f;
    float alpha = progress > 0.8f ? (1.0f - (progress - 0.8f) / 0.2f) : 1.0f;
    
    glm::mat4 model_mat = glm::mat4(1.0f);
    model_mat = glm::translate(model_mat, glm::vec3(pos_x, pos_y, pos_z));
    model_mat = glm::rotate(model_mat, spin, glm::vec3(0.0f, 1.0f, 0.0f));
    model_mat = glm::rotate(model_mat, spin * 0.7f, glm::vec3(1.0f, 0.0f, 0.0f));
    model_mat = glm::scale(model_mat, glm::vec3(scale * size_mod));
    
    float cx = (model->min_x + model->max_x) / 2.0f;
    float cy = (model->min_y + model->max_y) / 2.0f;
    float cz = (model->min_z + model->max_z) / 2.0f;
    model_mat = model_mat * glm::translate(glm::mat4(1.0f), glm::vec3(-cx, -cy, -cz));
    
    draw_model_effect(pass, cmd, model, model_mat, view, projection, camera_pos,
                      glm::vec4(1.0f, 0.8f, 0.5f, alpha),
                      glm::vec3(1.5f, 1.2f, 0.8f),
                      glm::vec3(0.6f, 0.4f, 0.2f));
}

void EffectRenderer::draw_orbit_aoe(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd,
                                       const std::string& model_name,
                                       float x, float y, float dir_x, float dir_y, float progress, float range,
                                       const glm::mat4& view, const glm::mat4& projection,
                                       const glm::vec3& camera_pos) {
    if (!model_manager_) return;

    Model* model = model_manager_->get_model(model_name);
    if (!model) return;
    
    int num_models = 3;
    float spin_speed = progress * 15.0f;
    float orbit_radius = range * 0.4f * std::min(1.0f, progress * 2.0f);
    float terrain_y = get_terrain_height(x, y);
    float base_height = terrain_y + 35.0f + std::sin(progress * 3.14159f) * 20.0f;
    
    float scale = 12.0f / model->max_dimension();
    float alpha = progress > 0.7f ? (1.0f - (progress - 0.7f) / 0.3f) : 1.0f;
    
    for (int i = 0; i < num_models; ++i) {
        float angle = spin_speed + (i * 2.0f * 3.14159f / num_models);
        float pos_x = x + std::cos(angle) * orbit_radius;
        float pos_z = y + std::sin(angle) * orbit_radius;
        float pos_y = base_height + std::sin(angle * 2.0f) * 10.0f;
        
        glm::mat4 model_mat = glm::mat4(1.0f);
        model_mat = glm::translate(model_mat, glm::vec3(pos_x, pos_y, pos_z));
        model_mat = glm::rotate(model_mat, angle + 1.57f, glm::vec3(0.0f, 1.0f, 0.0f));
        model_mat = glm::rotate(model_mat, 0.3f, glm::vec3(1.0f, 0.0f, 0.0f));
        model_mat = glm::rotate(model_mat, spin_speed * 0.5f, glm::vec3(0.0f, 0.0f, 1.0f));
        model_mat = glm::scale(model_mat, glm::vec3(scale));
        
        float cx = (model->min_x + model->max_x) / 2.0f;
        float cy = (model->min_y + model->max_y) / 2.0f;
        float cz = (model->min_z + model->max_z) / 2.0f;
        model_mat = model_mat * glm::translate(glm::mat4(1.0f), glm::vec3(-cx, -cy, -cz));
        
        draw_model_effect(pass, cmd, model, model_mat, view, projection, camera_pos,
                          glm::vec4(1.0f, 1.0f, 0.8f, alpha),
                          glm::vec3(1.2f, 1.2f, 0.8f),
                          glm::vec3(0.5f, 0.5f, 0.3f));
    }
}

void EffectRenderer::draw_arrow(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd,
                                        const std::string& model_name,
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
    
    Model* model = model_manager_->get_model(model_name);
    if (model) {
        draw_model_effect(pass, cmd, model, model_mat, view, projection, camera_pos,
                          glm::vec4(0.6f, 0.4f, 0.2f, alpha),
                          glm::vec3(0.9f, 0.85f, 0.7f),
                          glm::vec3(0.4f, 0.35f, 0.3f));
    }
}

} // namespace mmo::engine::render
