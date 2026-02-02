#include "effect_renderer.hpp"
#include "../gpu/gpu_texture.hpp"
#include "../gpu/gpu_uniforms.hpp"
#include "../render_constants.hpp"
#include "SDL3/SDL_error.h"
#include "SDL3/SDL_gpu.h"
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

void EffectRenderer::draw_particle_effects(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd,
                                            const mmo::engine::systems::EffectSystem& effect_system,
                                            const glm::mat4& view, const glm::mat4& projection,
                                            const glm::vec3& camera_pos) {
    if (!pass || !cmd || !device_ || !pipeline_registry_) return;

    // Render all particles from all effects
    for (const auto& effect : effect_system.get_effects()) {
        for (const auto& emitter : effect.emitters) {
            for (const auto& particle : emitter.particles) {
                draw_particle(pass, cmd, particle, view, projection, camera_pos);
            }
        }
    }
}

void EffectRenderer::draw_particle(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd,
                                    const mmo::engine::systems::Particle& particle,
                                    const glm::mat4& view, const glm::mat4& projection,
                                    const glm::vec3& camera_pos) {
    if (!model_manager_) {
        return;
    }

    // Get the model for this particle
    Model* model = model_manager_->get_model(particle.model);
    if (!model) {
        return;
    }

    // Build transform matrix
    glm::mat4 model_mat = glm::mat4(1.0f);

    // Position
    model_mat = glm::translate(model_mat, particle.position);

    // Rotation (euler angles in radians)
    model_mat = glm::rotate(model_mat, particle.rotation.y, glm::vec3(0.0f, 1.0f, 0.0f));
    model_mat = glm::rotate(model_mat, particle.rotation.x, glm::vec3(1.0f, 0.0f, 0.0f));
    model_mat = glm::rotate(model_mat, particle.rotation.z, glm::vec3(0.0f, 0.0f, 1.0f));

    // Scale
    model_mat = glm::scale(model_mat, glm::vec3(particle.scale));

    // Center the model at origin
    float cx = (model->min_x + model->max_x) / 2.0f;
    float cy = (model->min_y + model->max_y) / 2.0f;
    float cz = (model->min_z + model->max_z) / 2.0f;
    model_mat = model_mat * glm::translate(glm::mat4(1.0f), glm::vec3(-cx, -cy, -cz));

    // Tint color includes opacity in alpha channel
    glm::vec4 tint_color = particle.color;
    tint_color.a = particle.opacity;

    // Light colors - use warm tones for particles
    glm::vec3 light_color = glm::vec3(1.2f, 1.1f, 0.9f);
    glm::vec3 ambient_color = glm::vec3(0.5f, 0.5f, 0.4f);

    // Render the particle model
    draw_model_effect(pass, cmd, model, model_mat, view, projection, camera_pos,
                      tint_color, light_color, ambient_color);
}

} // namespace mmo::engine::render
