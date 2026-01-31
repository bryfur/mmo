#pragma once

#include <glm/glm.hpp>
#include <functional>

namespace mmo::engine::systems {

// Camera mode presets
enum class CameraMode {
    Exploration,   // Wider FOV, slower follow, more freedom
    Combat,        // Tighter framing, faster response, lock-on support
    Cinematic,     // Smooth sweeping movements for cutscenes
    Sprint         // Pulled back, lower angle for running
};

// Camera shake types for different feedback
enum class ShakeType {
    Impact,        // Quick punch - enemy hit
    Heavy,         // Sustained rumble - big explosion
    Directional,   // Shake toward a direction - getting hit
    Subtle         // Breathing/idle micro-movements
};

/**
 * Abstract camera interface exposed to game/client code.
 * Hides internal camera implementation details.
 */
class CameraController {
public:
    virtual ~CameraController() = default;

    // Target tracking
    virtual void set_target(const glm::vec3& position) = 0;
    virtual void set_target_velocity(const glm::vec3& velocity) = 0;

    // Camera angles
    virtual void set_yaw(float degrees) = 0;
    virtual void set_pitch(float degrees) = 0;
    virtual void rotate_yaw(float delta_degrees) = 0;
    virtual void rotate_pitch(float delta_degrees) = 0;
    virtual void adjust_zoom(float delta) = 0;

    // Mode
    virtual void set_mode(CameraMode mode) = 0;

    // Combat
    virtual void set_in_combat(bool in_combat) = 0;
    virtual void set_combat_target(const glm::vec3* target) = 0;
    virtual void notify_attack() = 0;
    virtual void notify_hit(const glm::vec3& hit_direction, float damage) = 0;

    // Camera shake
    virtual void add_shake(ShakeType type, float intensity, float duration = 0.3f) = 0;
    virtual void add_directional_shake(const glm::vec3& direction, float intensity, float duration = 0.2f) = 0;

    // Environment callbacks
    using TerrainHeightFunc = std::function<float(float x, float z)>;
    virtual void set_terrain_height_func(TerrainHeightFunc func) = 0;
    virtual void set_screen_size(int width, int height) = 0;

    // Update
    virtual void update(float dt) = 0;

    // Read-only output
    virtual glm::mat4 get_view_matrix() const = 0;
    virtual glm::mat4 get_projection_matrix() const = 0;
    virtual glm::vec3 get_position() const = 0;
    virtual glm::vec3 get_forward() const = 0;
};

} // namespace mmo::engine::systems
