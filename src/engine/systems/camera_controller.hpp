#pragma once

#include "engine/scene/camera_state.hpp"
#include <functional>
#include <glm/glm.hpp>

namespace mmo::engine::systems {

// Configuration for camera behavior
struct CameraModeConfig {
    float distance = 280.0f;         // Base distance from target
    float height_offset = 90.0f;     // Height above target
    float shoulder_offset = 40.0f;   // Horizontal shoulder offset
    float fov = 55.0f;               // Field of view
    float position_lag = 0.001f;     // Position smoothing (0-1, lower = more lag)
    float rotation_lag = 0.001f;     // Rotation smoothing
    float look_ahead_dist = 60.0f;   // How far to look ahead based on velocity
    float pitch_min = -70.0f;        // Minimum pitch (looking up)
    float pitch_max = 70.0f;         // Maximum pitch (looking down)
    float auto_return_speed = 1.5f;  // Speed of auto-centering behind player
    bool auto_center_enabled = true; // Whether to auto-center
};

// Camera shake types for different feedback
enum class ShakeType {
    Impact,      // Quick punch
    Heavy,       // Sustained rumble
    Directional, // Shake along a direction
    Subtle       // Idle micro-movements
};

/**
 * Abstract camera interface exposed to game/client code.
 * Engine cameras only know about framing, smoothing, and generic
 * shake/trauma primitives. Game-specific feel (combat, hit-reactions)
 * is layered on top by client wrappers via add_shake/add_directional_shake.
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

    // Optional soft-focus target. Implementations may bias framing toward
    // this point (e.g. action-cam soft-lock). Pass nullptr to clear.
    virtual void set_focus_target(const glm::vec3* target) = 0;
    virtual void set_focus_strength(float strength_0_1) = 0;

    // Configuration
    virtual void set_config(const CameraModeConfig& config) = 0;
    virtual const CameraModeConfig& get_config() const = 0;

    // Generic shake / kick / trauma primitives. Combat feel is composed on top.
    virtual void add_shake(ShakeType type, float intensity, float duration = 0.3f) = 0;
    virtual void add_directional_shake(const glm::vec3& direction, float intensity, float duration = 0.2f) = 0;

    // FOV bias hooks (unitless deltas summed onto base FOV by the camera).
    virtual void set_fov_bias(float fov_delta) = 0;

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

    // Convenience: build a complete CameraState from current matrices/position
    virtual scene::CameraState get_camera_state() const {
        scene::CameraState state;
        state.view = get_view_matrix();
        state.projection = get_projection_matrix();
        state.view_projection = state.projection * state.view;
        state.position = get_position();
        return state;
    }
};

} // namespace mmo::engine::systems
