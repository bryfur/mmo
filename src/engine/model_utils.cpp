#include "engine/model_utils.hpp"
#include "engine/gpu/gpu_types.hpp"
#include "engine/model_loader.hpp"
#include <cmath>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace mmo::engine {

glm::mat4 build_model_transform(const Model& model, const glm::vec3& position, float yaw_radians, float target_size,
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

void compute_tangents_from_uvs(std::vector<gpu::Vertex3D>& vertices, const std::vector<uint32_t>& indices) {
    if (vertices.empty() || indices.size() < 3) {
        return;
    }

    std::vector<glm::vec3> tan(vertices.size(), glm::vec3(0.0f));
    std::vector<glm::vec3> btn(vertices.size(), glm::vec3(0.0f));

    const size_t tri_count = indices.size() / 3;
    for (size_t t = 0; t < tri_count; ++t) {
        uint32_t i0 = indices[t * 3 + 0];
        uint32_t i1 = indices[t * 3 + 1];
        uint32_t i2 = indices[t * 3 + 2];
        if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size()) {
            continue;
        }

        const glm::vec3& p0 = vertices[i0].position;
        const glm::vec3& p1 = vertices[i1].position;
        const glm::vec3& p2 = vertices[i2].position;
        const glm::vec2& w0 = vertices[i0].texcoord;
        const glm::vec2& w1 = vertices[i1].texcoord;
        const glm::vec2& w2 = vertices[i2].texcoord;

        glm::vec3 e1 = p1 - p0;
        glm::vec3 e2 = p2 - p0;
        float x1 = w1.x - w0.x;
        float x2 = w2.x - w0.x;
        float y1 = w1.y - w0.y;
        float y2 = w2.y - w0.y;
        float det = x1 * y2 - x2 * y1;
        if (std::abs(det) < 1e-8f) {
            continue;
        }
        float inv = 1.0f / det;
        glm::vec3 t_dir = (e1 * y2 - e2 * y1) * inv;
        glm::vec3 b_dir = (e2 * x1 - e1 * x2) * inv;

        tan[i0] += t_dir;
        tan[i1] += t_dir;
        tan[i2] += t_dir;
        btn[i0] += b_dir;
        btn[i1] += b_dir;
        btn[i2] += b_dir;
    }

    for (size_t i = 0; i < vertices.size(); ++i) {
        glm::vec3 n = vertices[i].normal;
        float nlen = glm::length(n);
        if (nlen < 1e-6f) {
            vertices[i].tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
            continue;
        }
        n /= nlen;

        glm::vec3 t = tan[i] - n * glm::dot(n, tan[i]);
        float tlen = glm::length(t);
        if (tlen < 1e-6f) {
            glm::vec3 axis = std::abs(n.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
            t = glm::normalize(glm::cross(axis, n));
        } else {
            t /= tlen;
        }

        float sign = (glm::dot(glm::cross(n, t), btn[i]) < 0.0f) ? -1.0f : 1.0f;
        vertices[i].tangent = glm::vec4(t.x, t.y, t.z, sign);
    }
}

} // namespace mmo::engine
