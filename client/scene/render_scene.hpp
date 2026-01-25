#pragma once

#include "common/protocol.hpp"
#include "common/ecs/components.hpp"
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <array>
#include <variant>

namespace mmo {

/**
 * Model render command data
 */
struct ModelCommand {
    std::string model_name;
    glm::mat4 transform;
    glm::vec4 tint = {1.0f, 1.0f, 1.0f, 1.0f};
    float attack_tilt = 0.0f;
    bool no_fog = false;
};

/**
 * Skinned/animated model render command data
 */
struct SkinnedModelCommand {
    std::string model_name;
    glm::mat4 transform;
    std::array<glm::mat4, 64> bone_matrices;
    glm::vec4 tint = {1.0f, 1.0f, 1.0f, 1.0f};
};

/**
 * Entity render command - encapsulates all data needed to render an entity
 */
struct EntityCommand {
    EntityState state;
    bool is_local = false;
};

/**
 * Shadow render command for entities
 */
struct EntityShadowCommand {
    EntityState state;
};

/**
 * Attack effect render command
 */
struct EffectCommand {
    ecs::AttackEffect effect;
};

/**
 * Generic render command using std::variant for type-safe storage
 * Only stores one command type at a time for memory efficiency
 */
using RenderCommandData = std::variant<
    ModelCommand,
    SkinnedModelCommand,
    EntityCommand,
    EntityShadowCommand,
    EffectCommand
>;

struct RenderCommand {
    RenderCommandData data;
    
    // Helper methods for type checking
    template<typename T>
    bool is() const { return std::holds_alternative<T>(data); }
    
    template<typename T>
    const T& get() const { return std::get<T>(data); }
    
    template<typename T>
    T& get() { return std::get<T>(data); }
};

/**
 * RenderScene collects all 3D world render commands.
 * Game logic populates this, then the Renderer consumes it to draw.
 * 
 * Benefits:
 * - Decouples what to render from how to render
 * - Enables command sorting/batching before rendering
 * - Makes rendering testable without GPU
 * - Single point of change for GPU migration
 */
class RenderScene {
public:
    RenderScene() = default;
    ~RenderScene() = default;
    
    /**
     * Clear all render commands. Call at start of each frame.
     */
    void clear();
    
    // ========== 3D World Commands ==========
    
    /**
     * Add a static 3D model to the scene
     */
    void add_model(const std::string& model_name, const glm::mat4& transform,
                   const glm::vec4& tint = {1.0f, 1.0f, 1.0f, 1.0f},
                   float attack_tilt = 0.0f, bool no_fog = false);
    
    /**
     * Add a skinned/animated model to the scene
     */
    void add_skinned_model(const std::string& model_name, const glm::mat4& transform,
                           const std::array<glm::mat4, 64>& bone_matrices,
                           const glm::vec4& tint = {1.0f, 1.0f, 1.0f, 1.0f});
    
    /**
     * Add a game entity to the scene
     */
    void add_entity(const EntityState& state, bool is_local);
    
    /**
     * Add an entity shadow to the shadow pass
     */
    void add_entity_shadow(const EntityState& state);
    
    /**
     * Add an attack effect to the scene
     */
    void add_effect(const ecs::AttackEffect& effect);
    
    // ========== World Element Flags ==========
    // These indicate which world elements should be rendered
    
    void set_draw_skybox(bool draw) { draw_skybox_ = draw; }
    void set_draw_mountains(bool draw) { draw_mountains_ = draw; }
    void set_draw_rocks(bool draw) { draw_rocks_ = draw; }
    void set_draw_trees(bool draw) { draw_trees_ = draw; }
    void set_draw_ground(bool draw) { draw_ground_ = draw; }
    void set_draw_grass(bool draw) { draw_grass_ = draw; }
    void set_draw_mountain_shadows(bool draw) { draw_mountain_shadows_ = draw; }
    void set_draw_tree_shadows(bool draw) { draw_tree_shadows_ = draw; }
    
    bool should_draw_skybox() const { return draw_skybox_; }
    bool should_draw_mountains() const { return draw_mountains_; }
    bool should_draw_rocks() const { return draw_rocks_; }
    bool should_draw_trees() const { return draw_trees_; }
    bool should_draw_ground() const { return draw_ground_; }
    bool should_draw_grass() const { return draw_grass_; }
    bool should_draw_mountain_shadows() const { return draw_mountain_shadows_; }
    bool should_draw_tree_shadows() const { return draw_tree_shadows_; }
    
    // ========== Command Access ==========
    
    const std::vector<RenderCommand>& commands() const { return commands_; }
    const std::vector<EntityCommand>& entities() const { return entities_; }
    const std::vector<EntityShadowCommand>& entity_shadows() const { return entity_shadows_; }
    const std::vector<EffectCommand>& effects() const { return effects_; }
    
private:
    std::vector<RenderCommand> commands_;
    std::vector<EntityCommand> entities_;
    std::vector<EntityShadowCommand> entity_shadows_;
    std::vector<EffectCommand> effects_;
    
    // World element flags
    bool draw_skybox_ = true;
    bool draw_mountains_ = true;
    bool draw_rocks_ = true;
    bool draw_trees_ = true;
    bool draw_ground_ = true;
    bool draw_grass_ = true;
    bool draw_mountain_shadows_ = true;
    bool draw_tree_shadows_ = true;
};

} // namespace mmo
