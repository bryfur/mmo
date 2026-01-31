#include "camera_system.hpp"
#include "engine/systems/camera_controller.hpp"
#include "glm/common.hpp"
#include "glm/ext/matrix_clip_space.hpp"
#include "glm/ext/matrix_transform.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/geometric.hpp"
#include "glm/trigonometric.hpp"
#include <algorithm>
#include <cmath>

namespace mmo::engine::systems {

CameraSystem::CameraSystem() {
    // Initialize mode configurations
    
    // Exploration mode - relaxed, cinematic feel
    exploration_config_ = {
        .distance = 280.0f,
        .height_offset = 90.0f,
        .shoulder_offset = 40.0f,  // Over-the-shoulder offset
        .fov = 55.0f,
        .position_lag = 0.001f,    // No lag (original: 0.08f)
        .rotation_lag = 0.001f,    // No lag (original: 0.08f)
        .look_ahead_dist = 60.0f,   // Look ahead when moving
        .pitch_min = -70.0f,
        .pitch_max = 70.0f,
        .auto_return_speed = 1.5f,
        .auto_center_enabled = true
    };
    
    // Combat mode - tight, responsive, action-focused
    combat_config_ = {
        .distance = 220.0f,         // Closer for better combat awareness
        .height_offset = 75.0f,
        .shoulder_offset = 50.0f,   // More offset for over-the-shoulder aiming
        .fov = 52.0f,               // Slightly narrower for focus
        .position_lag = 0.001f,     // No lag (original: 0.08f)
        .rotation_lag = 0.001f,     // No lag (original: 0.05f)
        .look_ahead_dist = 40.0f,
        .pitch_min = -70.0f,
        .pitch_max = 70.0f,
        .auto_return_speed = 2.5f,
        .auto_center_enabled = false  // Don't auto-center during combat
    };
    
    // Cinematic mode - smooth, sweeping
    cinematic_config_ = {
        .distance = 350.0f,
        .height_offset = 100.0f,
        .shoulder_offset = 0.0f,    // Centered for cinematic framing
        .fov = 50.0f,
        .position_lag = 0.001f,     // No lag (original: 0.25f)
        .rotation_lag = 0.001f,     // No lag (original: 0.2f)
        .look_ahead_dist = 80.0f,
        .pitch_min = -45.0f,
        .pitch_max = 70.0f,
        .auto_return_speed = 0.5f,
        .auto_center_enabled = false
    };
    
    // Sprint mode - dynamic, pulled back
    sprint_config_ = {
        .distance = 320.0f,         // Further back
        .height_offset = 70.0f,     // Lower angle
        .shoulder_offset = 30.0f,   // Slight offset while sprinting
        .fov = 62.0f,               // Wider for speed sensation
        .position_lag = 0.001f,     // No lag (original: 0.12f)
        .rotation_lag = 0.001f,     // No lag (original: 0.1f)
        .look_ahead_dist = 100.0f,  // Strong look-ahead
        .pitch_min = -70.0f,
        .pitch_max = 70.0f,
        .auto_return_speed = 3.0f,
        .auto_center_enabled = true
    };
    
    // Initialize with exploration settings
    input_distance_ = exploration_config_.distance;
    current_distance_ = exploration_config_.distance;
    base_fov_ = exploration_config_.fov;
    current_fov_ = exploration_config_.fov;
}

const CameraModeConfig& CameraSystem::get_config(CameraMode mode) const {
    switch (mode) {
        case CameraMode::Exploration: return exploration_config_;
        case CameraMode::Combat: return combat_config_;
        case CameraMode::Cinematic: return cinematic_config_;
        case CameraMode::Sprint: return sprint_config_;
    }
    return exploration_config_;
}

void CameraSystem::update(float dt) {
    // Clamp dt to avoid instability
    dt = std::min(dt, 0.1f);
    
    // Track total time for idle breathing effect
    static float total_time = 0.0f;
    total_time += dt;
    
    // Update in proper order
    update_mode_transition(dt);
    update_input_smoothing(dt);
    update_look_ahead(dt);
    update_auto_centering(dt);
    update_soft_lock(dt);
    update_camera_position(dt);
    update_collision_avoidance(dt);
    update_camera_shake(dt);
    update_dynamic_fov(dt);
        
    compute_matrices();
    
    // Reset per-frame flags
    had_input_this_frame_ = false;
}

void CameraSystem::update_mode_transition(float dt) {
    if (current_mode_ != target_mode_) {
        mode_transition_ += mode_transition_speed_ * dt;
        if (mode_transition_ >= 1.0f) {
            mode_transition_ = 1.0f;
            current_mode_ = target_mode_;
        }
    }
}

void CameraSystem::update_input_smoothing(float dt) {
    const auto& config = get_config(current_mode_);
    
    // Track time since last input for auto-centering
    if (had_input_this_frame_) {
        time_since_input_ = 0.0f;
        auto_centering_active_ = false;
    } else {
        time_since_input_ += dt;
    }
    
    // Smooth yaw
    current_yaw_ = smooth_damp_angle(current_yaw_, input_yaw_, yaw_velocity_, 
                                      config.rotation_lag, dt);
    
    // Clamp pitch input
    float clamped_pitch = std::clamp(input_pitch_, config.pitch_min, config.pitch_max);
    
    // Smooth pitch
    current_pitch_ = smooth_damp_float(current_pitch_, clamped_pitch, pitch_velocity_,
                                        config.rotation_lag, dt);
    
    // Smooth distance
    current_distance_ = smooth_damp_float(current_distance_, 
                                           input_distance_ + collision_distance_offset_,
                                           distance_velocity_, 0.15f, dt);
}

void CameraSystem::update_look_ahead(float dt) {
    const auto& config = get_config(current_mode_);
    
    // Calculate look-ahead based on target velocity
    float speed = glm::length(target_velocity_);
    glm::vec3 velocity_dir = speed > 1.0f ? glm::normalize(target_velocity_) : glm::vec3(0.0f);
    
    // Scale look-ahead by speed, capped at max
    float look_ahead_factor = std::min(speed / 300.0f, 1.0f);  // Normalize by typical run speed
    glm::vec3 desired_look_ahead = velocity_dir * config.look_ahead_dist * look_ahead_factor;
    
    // Smooth look-ahead to avoid jerky changes
    look_ahead_offset_ = smooth_damp(look_ahead_offset_, desired_look_ahead, 
                                      look_ahead_vel_, 0.3f, dt);
}

void CameraSystem::update_auto_centering(float dt) {
    const auto& config = get_config(current_mode_);
    
    if (!config.auto_center_enabled) {
        return;
    }
    
    // Start auto-centering after delay with no input
    if (time_since_input_ > auto_center_delay_) {
        auto_centering_active_ = true;
    }
    
    if (!auto_centering_active_) {
        return;
    }
    
    // Calculate desired yaw based on movement direction
    float speed = glm::length(target_velocity_);
    if (speed < 10.0f) {
        return;  // Don't auto-center when barely moving
    }
    
    // Get movement direction angle
    float move_yaw = std::atan2(-target_velocity_.x, -target_velocity_.z) * 180.0f / 3.14159f;
    
    // Gradually blend toward movement direction
    float blend = config.auto_return_speed * dt;
    
    // Find shortest angle difference
    float diff = move_yaw - input_yaw_;
    while (diff > 180.0f) diff -= 360.0f;
    while (diff < -180.0f) diff += 360.0f;
    
    // Apply gradual rotation
    input_yaw_ += diff * blend;
    
    // Normalize
    while (input_yaw_ < 0.0f) input_yaw_ += 360.0f;
    while (input_yaw_ >= 360.0f) input_yaw_ -= 360.0f;
}

void CameraSystem::update_soft_lock(float dt) {
    // Soft-lock: gently bias camera toward combat target
    if (!in_combat_ || combat_target_ == nullptr) {
        soft_lock_strength_ = smooth_damp_float(soft_lock_strength_, 0.0f, 
                                                 fov_velocity_, 0.3f, dt);
        return;
    }
    
    // Increase soft-lock strength
    soft_lock_strength_ = smooth_damp_float(soft_lock_strength_, 0.6f,
                                             fov_velocity_, soft_lock_blend_speed_, dt);
    
    // Calculate angle to target
    glm::vec3 to_target = *combat_target_ - target_position_;
    float target_yaw = std::atan2(-to_target.x, -to_target.z) * 180.0f / 3.14159f;
    
    // Find shortest angle difference
    float diff = target_yaw - input_yaw_;
    while (diff > 180.0f) diff -= 360.0f;
    while (diff < -180.0f) diff += 360.0f;
    
    // Apply soft lock bias
    input_yaw_ += diff * soft_lock_strength_ * dt * 2.0f;
    
    // Normalize
    while (input_yaw_ < 0.0f) input_yaw_ += 360.0f;
    while (input_yaw_ >= 360.0f) input_yaw_ -= 360.0f;
}

void CameraSystem::update_camera_position(float dt) {
    const auto& config = get_config(current_mode_);
    
    // Smooth target position (player following)
    smoothed_target_ = smooth_damp(smoothed_target_, target_position_,
                                    target_smooth_vel_, config.position_lag, dt);
    
    // Get terrain height at smoothed target
    float terrain_y = 0.0f;
    if (get_terrain_height_) {
        terrain_y = get_terrain_height_(smoothed_target_.x, smoothed_target_.z);
    }
    
    // Calculate look-at target (above the actual target position)
    glm::vec3 base_look_at = smoothed_target_;
    base_look_at.y = terrain_y + config.height_offset;
    
    // Add look-ahead
    look_at_target_ = base_look_at + look_ahead_offset_;
    
    // Smooth look-at target
    current_look_at_ = smooth_damp(current_look_at_, look_at_target_,
                                    look_at_velocity_, config.position_lag * 0.5f, dt);
    
    // Calculate camera position from angles and distance
    float yaw_rad = glm::radians(current_yaw_);
    float pitch_rad = glm::radians(current_pitch_);
    
    // Spherical to Cartesian for camera offset
    float horizontal_dist = current_distance_ * std::cos(pitch_rad);
    float vertical_dist = current_distance_ * std::sin(pitch_rad);
    
    // Camera position relative to target
    float cam_offset_x = std::sin(yaw_rad) * horizontal_dist;
    float cam_offset_z = std::cos(yaw_rad) * horizontal_dist;
    
    // Apply shoulder offset (perpendicular to view direction)
    float right_x = std::cos(yaw_rad);
    float right_z = -std::sin(yaw_rad);
    cam_offset_x += right_x * config.shoulder_offset;
    cam_offset_z += right_z * config.shoulder_offset;
    
    // Calculate ideal camera position
    ideal_camera_pos_ = glm::vec3(
        current_look_at_.x + cam_offset_x,
        current_look_at_.y + vertical_dist,
        current_look_at_.z + cam_offset_z
    );
    
    // Smooth camera position
    current_camera_pos_ = smooth_damp(current_camera_pos_, ideal_camera_pos_,
                                       camera_pos_velocity_, config.position_lag, dt);
}

void CameraSystem::update_collision_avoidance(float dt) {
    final_camera_pos_ = current_camera_pos_;
    
    // === Terrain collision ===
    if (get_terrain_height_) {
        // Check terrain height at camera position
        float cam_terrain_y = get_terrain_height_(current_camera_pos_.x, current_camera_pos_.z);
        
        // Check several points along camera arm for terrain intersection
        glm::vec3 ray_dir = glm::normalize(current_camera_pos_ - current_look_at_);
        float ray_length = glm::length(current_camera_pos_ - current_look_at_);
        float min_clearance = current_camera_pos_.y - cam_terrain_y;
        
        for (float t = 0.2f; t <= 1.0f; t += 0.15f) {
            glm::vec3 check_pos = current_look_at_ + ray_dir * (ray_length * t);
            float check_terrain_y = get_terrain_height_(check_pos.x, check_pos.z);
            float clearance = check_pos.y - check_terrain_y;
            min_clearance = std::min(min_clearance, clearance);
        }
        
        // Raise camera if clipping terrain
        if (min_clearance < min_ground_clearance_) {
            float adjustment = min_ground_clearance_ - min_clearance;
            final_camera_pos_.y += adjustment;
        }
        
        // Final ground check at actual camera position
        float final_terrain_y = get_terrain_height_(final_camera_pos_.x, final_camera_pos_.z);
        if (final_camera_pos_.y < final_terrain_y + min_ground_clearance_) {
            final_camera_pos_.y = final_terrain_y + min_ground_clearance_;
        }
    }
    
    // === Wall/obstacle collision ===
    if (check_collision_) {
        glm::vec3 hit_point;
        if (check_collision_(current_look_at_, final_camera_pos_, hit_point)) {
            // Pull camera in front of obstacle
            glm::vec3 to_camera = glm::normalize(final_camera_pos_ - current_look_at_);
            float hit_distance = glm::length(hit_point - current_look_at_);
            
            // Place camera slightly in front of hit point
            float safe_distance = std::max(hit_distance - 20.0f, min_distance_ * 0.3f);
            final_camera_pos_ = current_look_at_ + to_camera * safe_distance;
            
            // Track collision offset for smooth pull-in
            float desired_offset = safe_distance - input_distance_;
            if (desired_offset < collision_distance_offset_) {
                // Pull in quickly
                collision_distance_offset_ = glm::mix(collision_distance_offset_, 
                                                       desired_offset, 
                                                       collision_pull_in_speed_ * dt);
            }
        } else {
            // Smoothly return to normal distance when not colliding
            collision_distance_offset_ = glm::mix(collision_distance_offset_, 
                                                   0.0f, 
                                                   collision_push_out_speed_ * dt);
        }
    } else {
        collision_distance_offset_ = glm::mix(collision_distance_offset_, 0.0f, 3.0f * dt);
    }
}

void CameraSystem::update_camera_shake(float dt) {
    shake_offset_ = glm::vec3(0.0f);
    
    // Update and accumulate all active shakes
    auto it = active_shakes_.begin();
    while (it != active_shakes_.end()) {
        it->elapsed += dt;
        
        if (it->elapsed >= it->duration) {
            it = active_shakes_.erase(it);
            continue;
        }
        
        // Calculate shake intensity with falloff
        float progress = it->elapsed / it->duration;
        float falloff = 1.0f - progress * progress;  // Quadratic falloff
        float intensity = it->intensity * falloff;
        
        // Generate shake based on type
        glm::vec3 shake(0.0f);
        float time = it->elapsed * it->frequency;
        
        switch (it->type) {
            case ShakeType::Impact:
                // Quick but subtle punch
                shake.x = std::sin(time * 40.0f) * intensity * 0.5f;
                shake.y = std::cos(time * 45.0f) * intensity * 0.3f;
                shake.z = std::sin(time * 35.0f + 1.0f) * intensity * 0.2f;
                break;
                
            case ShakeType::Heavy:
                // Low frequency rumble - subtle
                shake.x = std::sin(time * 12.0f) * intensity * 0.4f;
                shake.y = std::cos(time * 10.0f) * intensity * 0.3f;
                shake.z = std::sin(time * 14.0f + 0.5f) * intensity * 0.2f;
                break;
                
            case ShakeType::Directional:
                // Shake toward a specific direction - subtle
                shake = it->direction * std::sin(time * 25.0f) * intensity * 0.4f;
                break;
                
            case ShakeType::Subtle:
                // Very subtle idle movement
                shake.x = std::sin(time * 2.0f) * intensity * 0.1f;
                shake.y = std::cos(time * 1.5f) * intensity * 0.08f;
                break;
        }
        
        shake_offset_ += shake;
        ++it;
    }
    
    // Apply shake to final camera position
    final_camera_pos_ += shake_offset_;
}

void CameraSystem::update_dynamic_fov(float dt) {
    const auto& config = get_config(current_mode_);
    
    // Base FOV from current mode
    float target_fov = config.fov;
    
    // Speed-based FOV increase (sprint feeling)
    float speed = glm::length(target_velocity_);
    float speed_factor = std::min(speed / 400.0f, 1.0f);
    target_fov += sprint_fov_bonus_ * speed_factor * speed_factor;
    
    // Combat FOV reduction (focus)
    if (in_combat_) {
        target_fov += combat_fov_reduction_;
    }
    
    // Smooth FOV changes
    current_fov_ = smooth_damp_float(current_fov_, target_fov, fov_velocity_, 0.2f, dt);
}

void CameraSystem::compute_matrices() {
    // Compute camera orientation vectors
    float yaw_rad = glm::radians(current_yaw_);
    
    // Forward direction (where camera is looking)
    camera_forward_ = glm::normalize(current_look_at_ - final_camera_pos_);
    
    // Right vector (perpendicular to forward, in XZ plane)
    camera_right_ = glm::normalize(glm::cross(camera_forward_, glm::vec3(0.0f, 1.0f, 0.0f)));
    
    // Up vector
    camera_up_ = glm::normalize(glm::cross(camera_right_, camera_forward_));
    
    // View matrix
    view_matrix_ = glm::lookAt(final_camera_pos_, current_look_at_, glm::vec3(0.0f, 1.0f, 0.0f));
    
    // Projection matrix
    float aspect = static_cast<float>(screen_width_) / static_cast<float>(screen_height_);
    projection_matrix_ = glm::perspective(glm::radians(current_fov_), aspect, 5.0f, 15000.0f);
}

// === Smooth damping functions (critically-damped spring) ===

glm::vec3 CameraSystem::smooth_damp(const glm::vec3& current, const glm::vec3& target,
                                     glm::vec3& velocity, float smoothTime, float dt) {
    // Based on Game Programming Gems 4, critically damped spring
    smoothTime = std::max(0.0001f, smoothTime);
    float omega = 2.0f / smoothTime;
    float x = omega * dt;
    float exp_factor = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);
    
    glm::vec3 delta = current - target;
    glm::vec3 temp = (velocity + delta * omega) * dt;
    velocity = (velocity - temp * omega) * exp_factor;
    
    return target + (delta + temp) * exp_factor;
}

float CameraSystem::smooth_damp_angle(float current, float target, float& velocity,
                                       float smoothTime, float dt) {
    // Handle angle wrapping
    float diff = target - current;
    while (diff > 180.0f) diff -= 360.0f;
    while (diff < -180.0f) diff += 360.0f;
    
    target = current + diff;
    float result = smooth_damp_float(current, target, velocity, smoothTime, dt);
    
    // Normalize result
    while (result < 0.0f) result += 360.0f;
    while (result >= 360.0f) result -= 360.0f;
    
    return result;
}

float CameraSystem::smooth_damp_float(float current, float target, float& velocity,
                                       float smoothTime, float dt) {
    smoothTime = std::max(0.0001f, smoothTime);
    float omega = 2.0f / smoothTime;
    float x = omega * dt;
    float exp_factor = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);
    
    float delta = current - target;
    float temp = (velocity + delta * omega) * dt;
    velocity = (velocity - temp * omega) * exp_factor;
    
    return target + (delta + temp) * exp_factor;
}

// === Public interface ===

void CameraSystem::set_target(const glm::vec3& position) {
    target_position_ = position;
}

void CameraSystem::set_target_velocity(const glm::vec3& velocity) {
    target_velocity_ = velocity;
}

void CameraSystem::rotate_yaw(float delta_degrees) {
    input_yaw_ += delta_degrees;
    while (input_yaw_ < 0.0f) input_yaw_ += 360.0f;
    while (input_yaw_ >= 360.0f) input_yaw_ -= 360.0f;
    had_input_this_frame_ = true;
}

void CameraSystem::rotate_pitch(float delta_degrees) {
    const auto& config = get_config(current_mode_);
    input_pitch_ = std::clamp(input_pitch_ + delta_degrees, config.pitch_min, config.pitch_max);
    had_input_this_frame_ = true;
}

void CameraSystem::set_yaw(float degrees) {
    input_yaw_ = degrees;
    while (input_yaw_ < 0.0f) input_yaw_ += 360.0f;
    while (input_yaw_ >= 360.0f) input_yaw_ -= 360.0f;
}

void CameraSystem::set_pitch(float degrees) {
    const auto& config = get_config(current_mode_);
    input_pitch_ = std::clamp(degrees, config.pitch_min, config.pitch_max);
}

void CameraSystem::adjust_zoom(float delta) {
    input_distance_ = std::clamp(input_distance_ + delta, min_distance_, max_distance_);
    had_input_this_frame_ = true;
}

void CameraSystem::set_mode(CameraMode mode) {
    if (target_mode_ != mode) {
        target_mode_ = mode;
        mode_transition_ = 0.0f;
        
        // Update base distance for the new mode
        input_distance_ = get_config(mode).distance;
    }
}

void CameraSystem::set_combat_target(const glm::vec3* target) {
    combat_target_ = target;
}

void CameraSystem::set_in_combat(bool in_combat) {
    if (in_combat != in_combat_) {
        in_combat_ = in_combat;
        if (in_combat) {
            set_mode(CameraMode::Combat);
        } else {
            set_mode(CameraMode::Exploration);
        }
    }
}

void CameraSystem::notify_attack() {
    // Very small punch when player attacks
    add_shake(ShakeType::Impact, 0.3f, 0.08f);
}

void CameraSystem::notify_hit(const glm::vec3& hit_direction, float damage) {
    // Directional shake when hit - very subtle
    float intensity = std::min(damage / 100.0f, 1.5f);
    add_directional_shake(hit_direction, intensity, 0.15f);
}

void CameraSystem::add_shake(ShakeType type, float intensity, float duration) {
    if (active_shakes_.size() >= MAX_ACTIVE_SHAKES) {
        active_shakes_.pop_front();
    }
    
    CameraShake shake;
    shake.type = type;
    shake.intensity = intensity;
    shake.duration = duration;
    shake.elapsed = 0.0f;
    shake.direction = glm::vec3(0.0f);
    
    // Set frequency based on type
    switch (type) {
        case ShakeType::Impact: shake.frequency = 1.0f; break;
        case ShakeType::Heavy: shake.frequency = 1.0f; break;
        case ShakeType::Subtle: shake.frequency = 1.0f; break;
        default: shake.frequency = 1.0f; break;
    }
    
    active_shakes_.push_back(shake);
}

void CameraSystem::add_directional_shake(const glm::vec3& direction, float intensity, float duration) {
    if (active_shakes_.size() >= MAX_ACTIVE_SHAKES) {
        active_shakes_.pop_front();
    }
    
    CameraShake shake;
    shake.type = ShakeType::Directional;
    shake.intensity = intensity;
    shake.duration = duration;
    shake.elapsed = 0.0f;
    shake.direction = glm::length(direction) > 0.001f ? glm::normalize(direction) : glm::vec3(1, 0, 0);
    shake.frequency = 1.0f;
    
    active_shakes_.push_back(shake);
}

void CameraSystem::set_screen_size(int width, int height) {
    screen_width_ = std::max(1, width);
    screen_height_ = std::max(1, height);
}

} // namespace mmo::engine::systems
