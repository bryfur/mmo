#pragma once

#include "render_context.hpp"
#include "../model_loader.hpp"
#include "common/protocol.hpp"
#include "common/ecs/components.hpp"
#include <bgfx/bgfx.h>
#include <glm/glm.hpp>
#include <functional>

namespace mmo {

/**
 * EffectRenderer handles visual attack effects using bgfx:
 * - Warrior sword slash
 * - Mage fireball beam
 * - Paladin AOE circle
 * - Archer arrow projectile
 */
class EffectRenderer {
public:
    EffectRenderer();
    ~EffectRenderer();
    
    // Non-copyable
    EffectRenderer(const EffectRenderer&) = delete;
    EffectRenderer& operator=(const EffectRenderer&) = delete;
    
    /**
     * Initialize effect rendering resources.
     */
    bool init(ModelManager* model_manager);
    
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
     * Draw an attack effect based on its properties.
     */
    void draw_attack_effect(const ecs::AttackEffect& effect, 
                            bgfx::ViewId view_id,
                            const glm::mat4& view, const glm::mat4& projection);
    
private:
    float get_terrain_height(float x, float z) const;
    
    void draw_warrior_slash(float x, float y, float dir_x, float dir_y, float progress,
                            bgfx::ViewId view_id,
                            const glm::mat4& view, const glm::mat4& projection);
    void draw_mage_beam(float x, float y, float dir_x, float dir_y, float progress, float range,
                        bgfx::ViewId view_id,
                        const glm::mat4& view, const glm::mat4& projection);
    void draw_paladin_aoe(float x, float y, float dir_x, float dir_y, float progress, float range,
                          bgfx::ViewId view_id,
                          const glm::mat4& view, const glm::mat4& projection);
    void draw_archer_arrow(float x, float y, float dir_x, float dir_y, float progress, float range,
                           bgfx::ViewId view_id,
                           const glm::mat4& view, const glm::mat4& projection);
    
    void render_model(Model* model, const glm::mat4& model_mat, 
                      const glm::vec4& tint_color, float alpha,
                      bgfx::ViewId view_id);
    
    ModelManager* model_manager_ = nullptr;
    bgfx::ProgramHandle model_program_ = BGFX_INVALID_HANDLE;
    
    // Uniforms
    bgfx::UniformHandle u_model_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_lightDir_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_lightColor_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_ambientColor_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_tintColor_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_baseColor_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_params_ = BGFX_INVALID_HANDLE;  // fogEnabled, shadowsEnabled, ssaoEnabled, hasTexture
    bgfx::UniformHandle s_baseColorTexture_ = BGFX_INVALID_HANDLE;
    
    std::function<float(float, float)> terrain_height_func_;
};

} // namespace mmo
