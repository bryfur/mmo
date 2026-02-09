#pragma once

#include <glm/glm.hpp>
#include <cstdint>
#include <string>
#include <vector>
#include <array>
#include <variant>

namespace engine {
    struct EffectDefinition;
}

namespace mmo::engine::systems {
    class EffectSystem;
}

namespace mmo::engine::scene {

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
 * Generic render command using std::variant for type-safe storage
 */
using RenderCommandData = std::variant<
    ModelCommand,
    SkinnedModelCommand
>;

/**
 * Billboard UI element positioned in 3D world space.
 * The renderer projects to screen coordinates and scales by distance.
 */
struct Billboard3DCommand {
    float world_x, world_y, world_z;
    float width;
    float fill_ratio;
    uint32_t fill_color;
    uint32_t bg_color;
    uint32_t frame_color;
};

/**
 * Particle effect spawn command.
 * Tells the renderer to spawn a particle effect this frame.
 */
struct ParticleEffectSpawnCommand {
    const ::engine::EffectDefinition* definition = nullptr;
    glm::vec3 position = {0, 0, 0};
    glm::vec3 direction = {1, 0, 0};
    float range = -1.0f;
};

struct RenderCommand {
    RenderCommandData data;

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
 * Contains only engine-level types. Game-specific entity rendering
 * is handled by the game layer outside of RenderScene.
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
     * Add a particle effect spawn command
     * @param definition Effect definition (from EffectRegistry)
     * @param position World position to spawn effect
     * @param direction Direction vector for directional effects
     * @param range Effect range/scale (-1 = use definition default)
     */
    void add_particle_effect_spawn(const ::engine::EffectDefinition* definition,
                                    const glm::vec3& position,
                                    const glm::vec3& direction = {1, 0, 0},
                                    float range = -1.0f);

    /**
     * Get all particle effect spawn commands for this frame
     */
    const std::vector<ParticleEffectSpawnCommand>& particle_effect_spawns() const {
        return particle_effect_spawns_;
    }

    /**
     * Clear particle effect spawn commands (called by renderer after consuming)
     */
    void clear_particle_effect_spawns() {
        particle_effect_spawns_.clear();
    }

    /**
     * Add a 3D billboard (projected to screen space during rendering)
     */
    void add_billboard_3d(float world_x, float world_y, float world_z,
                          float width, float fill_ratio,
                          uint32_t fill_color, uint32_t bg_color, uint32_t frame_color);

    // ========== World Element Flags ==========

    void set_draw_skybox(bool draw) { draw_skybox_ = draw; }
    void set_draw_rocks(bool draw) { draw_rocks_ = draw; }
    void set_draw_trees(bool draw) { draw_trees_ = draw; }
    void set_draw_ground(bool draw) { draw_ground_ = draw; }
    void set_draw_grass(bool draw) { draw_grass_ = draw; }
    bool should_draw_skybox() const { return draw_skybox_; }
    bool should_draw_rocks() const { return draw_rocks_; }
    bool should_draw_trees() const { return draw_trees_; }
    bool should_draw_ground() const { return draw_ground_; }
    bool should_draw_grass() const { return draw_grass_; }

    bool has_3d_content() const {
        return draw_skybox_ || draw_ground_ || draw_grass_ ||
               !commands_.empty();
    }

    // ========== Command Access ==========

    const std::vector<RenderCommand>& commands() const { return commands_; }
    const std::vector<Billboard3DCommand>& billboards() const { return billboards_; }

private:
    std::vector<RenderCommand> commands_;
    std::vector<Billboard3DCommand> billboards_;
    std::vector<ParticleEffectSpawnCommand> particle_effect_spawns_;

    bool draw_skybox_ = false;
    bool draw_rocks_ = false;
    bool draw_trees_ = false;
    bool draw_ground_ = false;
    bool draw_grass_ = false;
};

} // namespace mmo::engine::scene
