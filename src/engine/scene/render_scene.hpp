#pragma once

#include "engine/effect_types.hpp"
#include <glm/glm.hpp>
#include <cstdint>
#include <string>
#include <vector>
#include <array>
#include <variant>

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
     * Add an attack effect to the scene
     */
    void add_effect(const engine::EffectInstance& effect);

    /**
     * Add a 3D billboard (projected to screen space during rendering)
     */
    void add_billboard_3d(float world_x, float world_y, float world_z,
                          float width, float fill_ratio,
                          uint32_t fill_color, uint32_t bg_color, uint32_t frame_color);

    // ========== World Element Flags ==========

    void set_draw_skybox(bool draw) { draw_skybox_ = draw; }
    void set_draw_mountains(bool draw) { draw_mountains_ = draw; }
    void set_draw_rocks(bool draw) { draw_rocks_ = draw; }
    void set_draw_trees(bool draw) { draw_trees_ = draw; }
    void set_draw_ground(bool draw) { draw_ground_ = draw; }
    void set_draw_grass(bool draw) { draw_grass_ = draw; }
    bool should_draw_skybox() const { return draw_skybox_; }
    bool should_draw_mountains() const { return draw_mountains_; }
    bool should_draw_rocks() const { return draw_rocks_; }
    bool should_draw_trees() const { return draw_trees_; }
    bool should_draw_ground() const { return draw_ground_; }
    bool should_draw_grass() const { return draw_grass_; }

    bool has_3d_content() const {
        return draw_skybox_ || draw_ground_ || draw_grass_ || draw_mountains_ ||
               !effects_.empty() || !commands_.empty();
    }

    // ========== Command Access ==========

    const std::vector<RenderCommand>& commands() const { return commands_; }
    const std::vector<engine::EffectInstance>& effects() const { return effects_; }
    const std::vector<Billboard3DCommand>& billboards() const { return billboards_; }

private:
    std::vector<RenderCommand> commands_;
    std::vector<engine::EffectInstance> effects_;
    std::vector<Billboard3DCommand> billboards_;

    bool draw_skybox_ = false;
    bool draw_mountains_ = false;
    bool draw_rocks_ = false;
    bool draw_trees_ = false;
    bool draw_ground_ = false;
    bool draw_grass_ = false;
};

} // namespace mmo::engine::scene
