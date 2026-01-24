#pragma once

#include "../shader.hpp"
#include "../model_loader.hpp"
#include "common/protocol.hpp"
#include "common/ecs/components.hpp"
#include <GL/glew.h>
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
                            const glm::mat4& view, const glm::mat4& projection);
    
private:
    float get_terrain_height(float x, float z) const;
    
    void draw_warrior_slash(float x, float y, float dir_x, float dir_y, float progress,
                            const glm::mat4& view, const glm::mat4& projection);
    void draw_mage_beam(float x, float y, float dir_x, float dir_y, float progress, float range,
                        const glm::mat4& view, const glm::mat4& projection);
    void draw_paladin_aoe(float x, float y, float dir_x, float dir_y, float progress, float range,
                          const glm::mat4& view, const glm::mat4& projection);
    void draw_archer_arrow(float x, float y, float dir_x, float dir_y, float progress, float range,
                           const glm::mat4& view, const glm::mat4& projection);
    
    ModelManager* model_manager_ = nullptr;
    std::unique_ptr<Shader> model_shader_;
    std::function<float(float, float)> terrain_height_func_;
};

} // namespace mmo
