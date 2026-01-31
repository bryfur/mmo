#pragma once

#include "../model_loader.hpp"
#include "../gpu/gpu_device.hpp"
#include "../gpu/gpu_buffer.hpp"
#include "../gpu/gpu_pipeline.hpp"
#include "../gpu/pipeline_registry.hpp"
#include "engine/effect_types.hpp"
#include <SDL3/SDL_gpu.h>
#include <glm/glm.hpp>
#include <memory>
#include <functional>

namespace mmo {

/**
 * EffectRenderer handles visual attack effects:
 * - Warrior sword slash
 * - Mage fireball beam
 * - Paladin AOE circle
 * - Archer arrow projectile
 * 
 * SDL3 GPU Migration: This class now uses SDL3 GPU API instead of OpenGL.
 * - GL shaders replaced with PipelineRegistry pipelines
 * - Draw calls use SDL_DrawGPUIndexedPrimitives
 * - Mesh buffers come from ModelLoader's SDL3 GPU buffers
 */
class EffectRenderer {
public:
    EffectRenderer();
    ~EffectRenderer();
    
    // Non-copyable
    EffectRenderer(const EffectRenderer&) = delete;
    EffectRenderer& operator=(const EffectRenderer&) = delete;
    
    /**
     * Initialize effect rendering resources (SDL3 GPU API version).
     * @param device GPU device for resource creation
     * @param pipeline_registry Pipeline registry for shader pipelines
     * @param model_manager Model manager for effect models
     */
    bool init(gpu::GPUDevice& device, gpu::PipelineRegistry& pipeline_registry,
              ModelManager* model_manager);
    
    /**
     * Clean up resources.
     */
    void shutdown();
    
    /**
     * Set terrain height callback.
     */
    void set_terrain_height_func(std::function<float(float, float)> func) {
        terrain_height_func_ = std::move(func);
    }
    
    /**
     * Draw an attack effect based on its properties (SDL3 GPU API version).
     * @param pass Active render pass
     * @param cmd Command buffer for uniform uploads
     */
    void draw_attack_effect(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd,
                            const engine::EffectInstance& effect, 
                            const glm::mat4& view, const glm::mat4& projection,
                            const glm::vec3& camera_pos);
    
private:
    float get_terrain_height(float x, float z) const;
    
    void draw_model_effect(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd,
                           Model* model, const glm::mat4& model_mat,
                           const glm::mat4& view, const glm::mat4& projection,
                           const glm::vec3& camera_pos,
                           const glm::vec4& tint_color,
                           const glm::vec3& light_color,
                           const glm::vec3& ambient_color);
    
    void draw_warrior_slash(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd,
                            float x, float y, float dir_x, float dir_y, float progress,
                            const glm::mat4& view, const glm::mat4& projection,
                            const glm::vec3& camera_pos);
    void draw_mage_beam(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd,
                        float x, float y, float dir_x, float dir_y, float progress, float range,
                        const glm::mat4& view, const glm::mat4& projection,
                        const glm::vec3& camera_pos);
    void draw_paladin_aoe(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd,
                          float x, float y, float dir_x, float dir_y, float progress, float range,
                          const glm::mat4& view, const glm::mat4& projection,
                          const glm::vec3& camera_pos);
    void draw_archer_arrow(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd,
                           float x, float y, float dir_x, float dir_y, float progress, float range,
                           const glm::mat4& view, const glm::mat4& projection,
                           const glm::vec3& camera_pos);
    
    gpu::GPUDevice* device_ = nullptr;
    gpu::PipelineRegistry* pipeline_registry_ = nullptr;
    ModelManager* model_manager_ = nullptr;
    SDL_GPUSampler* sampler_ = nullptr;
    std::function<float(float, float)> terrain_height_func_;
};

} // namespace mmo
