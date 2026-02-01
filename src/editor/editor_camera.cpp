#include "editor_camera.hpp"
#include <glm/gtc/constants.hpp>
#include <algorithm>

namespace mmo::editor {

EditorCamera::EditorCamera() {
    update_vectors();
}

void EditorCamera::move_forward(float amount) {
    position_ += forward_ * amount;
}

void EditorCamera::move_right(float amount) {
    position_ += right_ * amount;
}

void EditorCamera::move_up(float amount) {
    position_.y += amount;  // World-space up, not camera-relative
}

void EditorCamera::rotate_yaw(float delta) {
    yaw_ += delta;
    // Wrap yaw to [-pi, pi]
    while (yaw_ > glm::pi<float>()) yaw_ -= 2.0f * glm::pi<float>();
    while (yaw_ < -glm::pi<float>()) yaw_ += 2.0f * glm::pi<float>();
    update_vectors();
}

void EditorCamera::rotate_pitch(float delta) {
    pitch_ += delta;
    // Clamp pitch to avoid gimbal lock
    pitch_ = std::clamp(pitch_, -glm::half_pi<float>() + 0.01f, glm::half_pi<float>() - 0.01f);
    update_vectors();
}

void EditorCamera::update(float dt) {
    // Currently no smoothing or constraints
    // Could add collision detection, ground clamp, etc. here
}

glm::mat4 EditorCamera::get_view_matrix() const {
    return glm::lookAt(position_, position_ + forward_, glm::vec3(0.0f, 1.0f, 0.0f));
}

glm::mat4 EditorCamera::get_projection_matrix(float aspect) const {
    return glm::perspective(glm::radians(fov_), aspect, 1.0f, 10000.0f);
}

glm::vec3 EditorCamera::get_forward() const {
    return forward_;
}

glm::vec3 EditorCamera::get_right() const {
    return right_;
}

glm::vec3 EditorCamera::get_up() const {
    return up_;
}

void EditorCamera::update_vectors() {
    // Calculate forward vector from yaw and pitch
    forward_.x = cos(pitch_) * sin(yaw_);
    forward_.y = sin(pitch_);
    forward_.z = cos(pitch_) * cos(yaw_);
    forward_ = glm::normalize(forward_);

    // Calculate right vector
    right_ = glm::normalize(glm::cross(forward_, glm::vec3(0.0f, 1.0f, 0.0f)));

    // Calculate up vector
    up_ = glm::normalize(glm::cross(right_, forward_));
}

} // namespace mmo::editor
