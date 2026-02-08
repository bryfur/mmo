#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace mmo::editor {

/**
 * Free-flying editor camera with WASD movement and mouse look.
 * Not bound to any entity - can freely navigate the 3D world.
 */
class EditorCamera {
public:
    EditorCamera();

    // Movement in camera-relative space
    void move_forward(float amount);
    void move_right(float amount);
    void move_up(float amount);

    // Camera rotation
    void rotate_yaw(float delta);
    void rotate_pitch(float delta);

    // Speed control
    void set_move_speed(float speed) { move_speed_ = speed; }
    float get_move_speed() const { return move_speed_; }

    // Update camera (apply smoothing, constraints)
    void update(float dt);

    // Camera matrices
    glm::mat4 get_view_matrix() const;
    glm::mat4 get_projection_matrix(float aspect) const;

    // Camera state
    glm::vec3 get_position() const { return position_; }
    glm::vec3 get_forward() const;
    glm::vec3 get_right() const;
    glm::vec3 get_up() const;

    float get_yaw() const { return yaw_; }
    float get_pitch() const { return pitch_; }

private:
    void update_vectors();

    glm::vec3 position_{4000.0f, 200.0f, 4000.0f};  // Start above town center
    glm::vec3 forward_{0.0f, 0.0f, -1.0f};
    glm::vec3 right_{1.0f, 0.0f, 0.0f};
    glm::vec3 up_{0.0f, 1.0f, 0.0f};

    float yaw_ = 0.0f;         // Horizontal rotation (radians)
    float pitch_ = -0.5f;      // Vertical rotation (radians), looking slightly down
    float move_speed_ = 500.0f;
    float fov_ = 60.0f;
};

} // namespace mmo::editor
