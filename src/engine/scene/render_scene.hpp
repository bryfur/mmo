#pragma once

#include "engine/model_loader.hpp"  // ModelHandle
#include "engine/render/lighting/light.hpp"
#include <glm/glm.hpp>
#include <cstdint>
#include <string>
#include <vector>
#include <array>

namespace mmo::engine {
    struct EffectDefinition;
}

namespace mmo::engine::systems {
    class EffectSystem;
}

namespace mmo::engine::scene {

/**
 * Model render command data (~140 bytes)
 */
struct ModelCommand {
    mmo::engine::ModelHandle model_handle = mmo::engine::INVALID_MODEL_HANDLE;
    std::string model_name;  // kept for debug display; not used in hot path lookups
    glm::mat4 transform;
    glm::vec4 tint = {1.0f, 1.0f, 1.0f, 1.0f};
    bool force_non_instanced = false;
    bool no_fog = false;
};

/**
 * Skinned/animated model render command data.
 * Bone matrices are stored as a const pointer to avoid copying 4KB per entity.
 * The pointed-to data must remain valid until the frame is rendered.
 */
struct SkinnedModelCommand {
    mmo::engine::ModelHandle model_handle = mmo::engine::INVALID_MODEL_HANDLE;
    std::string model_name;  // kept for debug display; not used in hot path lookups
    glm::mat4 transform;
    const std::array<glm::mat4, 128>* bone_matrices = nullptr;
    glm::vec4 tint = {1.0f, 1.0f, 1.0f, 1.0f};
};

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
    const mmo::engine::EffectDefinition* definition = nullptr;
    glm::vec3 position = {0, 0, 0};
    glm::vec3 direction = {1, 0, 0};
    float range = -1.0f;
};

/**
 * Debug line command: a single colored line segment in world space.
 */
struct DebugLineCommand {
    glm::vec3 start;
    glm::vec3 end;
    uint32_t color;  // RGBA packed
};

/**
 * RenderScene collects all 3D world render commands.
 * Game logic populates this, then the Renderer consumes it to draw.
 *
 * Model and skinned model commands are stored in separate vectors
 * for cache efficiency and to avoid variant size overhead.
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
     * Add a static 3D model to the scene (handle-based fast path, zero string alloc)
     */
    void add_model(mmo::engine::ModelHandle handle, const glm::mat4& transform,
                   const glm::vec4& tint = {1.0f, 1.0f, 1.0f, 1.0f},
                   bool force_non_instanced = false, bool no_fog = false);

    /**
     * Add a static 3D model to the scene (string-based, backward compat)
     */
    void add_model(std::string model_name, const glm::mat4& transform,
                   const glm::vec4& tint = {1.0f, 1.0f, 1.0f, 1.0f},
                   bool force_non_instanced = false, bool no_fog = false);

    /**
     * Add a skinned/animated model (handle-based fast path, zero string alloc).
     * bone_matrices must remain valid until the frame is rendered.
     */
    void add_skinned_model(mmo::engine::ModelHandle handle, const glm::mat4& transform,
                           const std::array<glm::mat4, 128>& bone_matrices,
                           const glm::vec4& tint = {1.0f, 1.0f, 1.0f, 1.0f});

    /**
     * Add a skinned/animated model (string-based, backward compat).
     * bone_matrices must remain valid until the frame is rendered.
     */
    void add_skinned_model(std::string model_name, const glm::mat4& transform,
                           const std::array<glm::mat4, 128>& bone_matrices,
                           const glm::vec4& tint = {1.0f, 1.0f, 1.0f, 1.0f});

    /**
     * Add a particle effect spawn command
     */
    void add_particle_effect_spawn(const mmo::engine::EffectDefinition* definition,
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

    // ========== Debug Drawing ==========

    /**
     * Add a single debug line in world space.
     */
    void add_debug_line(const glm::vec3& start, const glm::vec3& end, uint32_t color);

    /**
     * Add a wireframe sphere (3 circle outlines in XY, XZ, YZ planes).
     */
    void add_debug_sphere(const glm::vec3& center, float radius, uint32_t color, int segments = 16);

    /**
     * Add a wireframe axis-aligned box from min/max corners.
     */
    void add_debug_box(const glm::vec3& min, const glm::vec3& max, uint32_t color);

    /**
     * Get all debug line commands for this frame.
     */
    const std::vector<DebugLineCommand>& debug_lines() const { return debug_lines_; }

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
               !model_commands_.empty() || !skinned_commands_.empty();
    }

    // ========== Command Access ==========

    const std::vector<ModelCommand>& model_commands() const { return model_commands_; }
    const std::vector<SkinnedModelCommand>& skinned_commands() const { return skinned_commands_; }
    const std::vector<Billboard3DCommand>& billboards() const { return billboards_; }

    // ========== Dynamic Lights ==========

    void add_point_light(const mmo::engine::render::lighting::PointLight& l);
    void add_spot_light(const mmo::engine::render::lighting::SpotLight& l);
    const std::vector<mmo::engine::render::lighting::PointLight>& point_lights() const { return point_lights_; }
    const std::vector<mmo::engine::render::lighting::SpotLight>& spot_lights() const { return spot_lights_; }
    void clear_lights();

private:
    std::vector<ModelCommand> model_commands_;
    std::vector<SkinnedModelCommand> skinned_commands_;
    std::vector<Billboard3DCommand> billboards_;
    std::vector<ParticleEffectSpawnCommand> particle_effect_spawns_;
    std::vector<DebugLineCommand> debug_lines_;
    std::vector<mmo::engine::render::lighting::PointLight> point_lights_;
    std::vector<mmo::engine::render::lighting::SpotLight> spot_lights_;

    bool draw_skybox_ = false;
    bool draw_rocks_ = false;
    bool draw_trees_ = false;
    bool draw_ground_ = false;
    bool draw_grass_ = false;
};

} // namespace mmo::engine::scene
