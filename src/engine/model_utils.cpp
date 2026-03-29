#include "engine/model_utils.hpp"
#include "engine/model_loader.hpp"
#include <glm/gtc/matrix_transform.hpp>

namespace mmo::engine {

glm::mat4 build_model_transform(const Model& model, const glm::vec3& position,
                                float yaw_radians, float target_size,
                                float attack_tilt) {
    float scale = (target_size * 1.5f) / model.max_dimension();

    glm::mat4 mat = glm::translate(glm::mat4(1.0f), position);
    mat = glm::rotate(mat, yaw_radians, glm::vec3(0.0f, 1.0f, 0.0f));
    if (attack_tilt != 0.0f) {
        mat = glm::rotate(mat, attack_tilt, glm::vec3(1.0f, 0.0f, 0.0f));
    }
    mat = glm::scale(mat, glm::vec3(scale));

    float cx = (model.min_x + model.max_x) / 2.0f;
    float cy = model.min_y;
    float cz = (model.min_z + model.max_z) / 2.0f;
    mat = mat * glm::translate(glm::mat4(1.0f), glm::vec3(-cx, -cy, -cz));

    return mat;
}

} // namespace mmo::engine
