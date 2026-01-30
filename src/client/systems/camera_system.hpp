#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <functional>
#include <deque>

namespace mmo {

// Professional third-person action game camera system
// Inspired by: God of War, Horizon Zero Dawn, Ghost of Tsushima, The Last of Us Part II

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

struct CameraShake {
    ShakeType type;
    float intensity;
    float duration;
    float elapsed;
    glm::vec3 direction;  // For directional shakes
    float frequency;      // Oscillation frequency
};

// Configuration for each camera mode
struct CameraModeConfig {
    float distance;           // Base distance from target
    float height_offset;      // Height above target
    float shoulder_offset;    // Horizontal shoulder offset
    float fov;               // Field of view
    float position_lag;       // Position smoothing (0-1, lower = more lag)
    float rotation_lag;       // Rotation smoothing
    float look_ahead_dist;    // How far to look ahead based on velocity
    float pitch_min;          // Minimum pitch (looking up)
    float pitch_max;          // Maximum pitch (looking down)
    float auto_return_speed;  // Speed of auto-centering behind player
    bool auto_center_enabled; // Whether to auto-center
};

class CameraSystem {
public:
    CameraSystem();
    
    // Main update - call every frame with delta time
    void update(float dt);
    
    // Set the target to follow (player position in world space)
    void set_target(const glm::vec3& position);
    void set_target_velocity(const glm::vec3& velocity);
    
    // Manual camera control (from mouse/controller input)
    void rotate_yaw(float delta_degrees);
    void rotate_pitch(float delta_degrees);
    void set_yaw(float degrees);    // Direct set (no delta)
    void set_pitch(float degrees);  // Direct set (no delta)
    void adjust_zoom(float delta);
    float get_input_yaw() const { return input_yaw_; }
    float get_input_pitch() const { return input_pitch_; }
    
    // Mode switching with smooth transitions
    void set_mode(CameraMode mode);
    CameraMode get_mode() const { return current_mode_; }
    
    // Combat system integration
    void set_combat_target(const glm::vec3* target);  // nullptr to clear
    void set_in_combat(bool in_combat);
    void notify_attack();  // Player attacked
    void notify_hit(const glm::vec3& hit_direction, float damage);  // Player got hit
    
    // Camera shake
    void add_shake(ShakeType type, float intensity, float duration = 0.3f);
    void add_directional_shake(const glm::vec3& direction, float intensity, float duration = 0.2f);
    
    // Terrain collision callback - returns height at world XZ position
    using TerrainHeightFunc = std::function<float(float x, float z)>;
    void set_terrain_height_func(TerrainHeightFunc func) { get_terrain_height_ = func; }
    
    // Obstacle/wall collision callback - returns true if line of sight is blocked
    using CollisionCheckFunc = std::function<bool(const glm::vec3& from, const glm::vec3& to, glm::vec3& hit_point)>;
    void set_collision_func(CollisionCheckFunc func) { check_collision_ = func; }
    
    // Get computed matrices and values
    glm::mat4 get_view_matrix() const { return view_matrix_; }
    glm::mat4 get_projection_matrix() const { return projection_matrix_; }
    glm::vec3 get_position() const { return final_camera_pos_; }
    glm::vec3 get_target_position() const { return smoothed_target_; }  // Player/follow target position
    glm::vec3 get_forward() const { return camera_forward_; }
    glm::vec3 get_right() const { return camera_right_; }
    float get_yaw() const { return current_yaw_; }
    float get_pitch() const { return current_pitch_; }
    float get_current_fov() const { return current_fov_; }
    float get_current_distance() const { return current_distance_; }
    
    // Set screen dimensions for projection matrix
    void set_screen_size(int width, int height);
    
    // Debug visualization
    void set_debug_draw_enabled(bool enabled) { debug_draw_ = enabled; }
    
private:
    // Internal update stages
    void update_mode_transition(float dt);
    void update_input_smoothing(float dt);
    void update_look_ahead(float dt);
    void update_auto_centering(float dt);
    void update_soft_lock(float dt);
    void update_camera_position(float dt);
    void update_collision_avoidance(float dt);
    void update_camera_shake(float dt);
    void update_dynamic_fov(float dt);
    void compute_matrices();
    
    // Smooth damping helper (critically-damped spring)
    static glm::vec3 smooth_damp(const glm::vec3& current, const glm::vec3& target, 
                                  glm::vec3& velocity, float smoothTime, float dt);
    static float smooth_damp_angle(float current, float target, float& velocity, 
                                    float smoothTime, float dt);
    static float smooth_damp_float(float current, float target, float& velocity,
                                    float smoothTime, float dt);
    
    // Configuration for each mode
    CameraModeConfig exploration_config_;
    CameraModeConfig combat_config_;
    CameraModeConfig cinematic_config_;
    CameraModeConfig sprint_config_;
    const CameraModeConfig& get_config(CameraMode mode) const;
    
    // Current mode and transition
    CameraMode current_mode_ = CameraMode::Exploration;
    CameraMode target_mode_ = CameraMode::Exploration;
    float mode_transition_ = 1.0f;  // 0 = transitioning, 1 = arrived
    float mode_transition_speed_ = 3.0f;
    
    // Target tracking
    glm::vec3 target_position_ = glm::vec3(0.0f);
    glm::vec3 target_velocity_ = glm::vec3(0.0f);
    glm::vec3 smoothed_target_ = glm::vec3(0.0f);
    glm::vec3 target_smooth_vel_ = glm::vec3(0.0f);
    
    // Look-ahead
    glm::vec3 look_ahead_offset_ = glm::vec3(0.0f);
    glm::vec3 look_ahead_vel_ = glm::vec3(0.0f);
    
    // Camera angles (degrees)
    float input_yaw_ = 0.0f;       // Raw input yaw
    float input_pitch_ = 25.0f;    // Raw input pitch
    float current_yaw_ = 0.0f;     // Smoothed yaw
    float current_pitch_ = 25.0f;  // Smoothed pitch
    float yaw_velocity_ = 0.0f;
    float pitch_velocity_ = 0.0f;
    
    // Auto-centering
    float time_since_input_ = 0.0f;
    float auto_center_delay_ = 2.0f;  // Seconds before auto-centering starts
    bool auto_centering_active_ = false;
    
    // Distance/zoom
    float input_distance_ = 250.0f;
    float current_distance_ = 250.0f;
    float distance_velocity_ = 0.0f;
    float min_distance_ = 80.0f;
    float max_distance_ = 600.0f;
    
    // Collision avoidance
    float collision_pull_in_speed_ = 15.0f;
    float collision_push_out_speed_ = 5.0f;
    float collision_distance_offset_ = 0.0f;
    float min_ground_clearance_ = 25.0f;
    
    // Camera positions
    glm::vec3 ideal_camera_pos_ = glm::vec3(0.0f);
    glm::vec3 current_camera_pos_ = glm::vec3(0.0f);
    glm::vec3 final_camera_pos_ = glm::vec3(0.0f);
    glm::vec3 camera_pos_velocity_ = glm::vec3(0.0f);
    
    // Look target
    glm::vec3 look_at_target_ = glm::vec3(0.0f);
    glm::vec3 current_look_at_ = glm::vec3(0.0f);
    glm::vec3 look_at_velocity_ = glm::vec3(0.0f);
    
    // Camera orientation vectors
    glm::vec3 camera_forward_ = glm::vec3(0.0f, 0.0f, -1.0f);
    glm::vec3 camera_right_ = glm::vec3(1.0f, 0.0f, 0.0f);
    glm::vec3 camera_up_ = glm::vec3(0.0f, 1.0f, 0.0f);
    
    // Combat targeting
    bool in_combat_ = false;
    const glm::vec3* combat_target_ = nullptr;
    float soft_lock_strength_ = 0.0f;
    float soft_lock_blend_speed_ = 3.0f;
    
    // Dynamic FOV
    float base_fov_ = 55.0f;
    float current_fov_ = 55.0f;
    float fov_velocity_ = 0.0f;
    float sprint_fov_bonus_ = 8.0f;
    float combat_fov_reduction_ = -3.0f;
    
    // Camera shake
    std::deque<CameraShake> active_shakes_;
    glm::vec3 shake_offset_ = glm::vec3(0.0f);
    static constexpr size_t MAX_ACTIVE_SHAKES = 8;
    
    // Screen dimensions
    int screen_width_ = 1280;
    int screen_height_ = 720;
    
    // Output matrices
    glm::mat4 view_matrix_ = glm::mat4(1.0f);
    glm::mat4 projection_matrix_ = glm::mat4(1.0f);
    
    // Callbacks
    TerrainHeightFunc get_terrain_height_ = nullptr;
    CollisionCheckFunc check_collision_ = nullptr;
    
    // Debug
    bool debug_draw_ = false;
    
    // Input tracking
    bool had_input_this_frame_ = false;
};

} // namespace mmo
