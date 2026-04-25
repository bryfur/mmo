#pragma once

#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>

namespace mmo::engine::scene {

// Returns true if the 3x3 linear part has (approximately) uniform scale.
// For uniform scale, inverse-transpose(M) is M / s^2 which re-normalizes to M in
// the shader, so the expensive CPU inverse-transpose can be skipped.
inline bool has_uniform_scale(const glm::mat4& m) {
    const glm::vec3 c0(m[0]);
    const glm::vec3 c1(m[1]);
    const glm::vec3 c2(m[2]);
    const float s0 = glm::dot(c0, c0);
    const float s1 = glm::dot(c1, c1);
    const float s2 = glm::dot(c2, c2);
    const float max_s = std::max({s0, s1, s2});
    const float min_s = std::min({s0, s1, s2});
    // Relative tolerance ~0.1% on squared values (~0.05% on linear scale).
    return (max_s - min_s) <= max_s * 0.001f;
}

// Normal matrix with uniform-scale fast path.
// For uniform scale (the overwhelmingly common case) this skips a 4x4 inverse.
inline glm::mat4 compute_normal_matrix(const glm::mat4& model) {
    if (has_uniform_scale(model)) return model;
    return glm::transpose(glm::inverse(model));
}

// Max scale factor for bounding-sphere expansion. One sqrt instead of three.
inline float max_scale_factor(const glm::mat4& m) {
    const glm::vec3 c0(m[0]);
    const glm::vec3 c1(m[1]);
    const glm::vec3 c2(m[2]);
    const float s0 = glm::dot(c0, c0);
    const float s1 = glm::dot(c1, c1);
    const float s2 = glm::dot(c2, c2);
    return std::sqrt(std::max({s0, s1, s2}));
}

} // namespace mmo::engine::scene
