#pragma once

#include <glm/glm.hpp>
#include <cstdint>
#include <vector>

namespace mmo::engine {

struct Model;  // forward declare
namespace gpu { struct Vertex3D; }

// Build a standard model-to-world transform matrix.
// Applies: translate(position) * rotate(yaw) * scale(uniform) * center(-pivot)
// where pivot.x/z = bounding box center, pivot.y = min_y.
// Optional attack_tilt rotates around X axis (lean forward) after yaw.
glm::mat4 build_model_transform(const Model& model, const glm::vec3& position,
                                float yaw_radians, float target_size,
                                float attack_tilt = 0.0f);

// Per-vertex tangent generation from UV gradients. Used as a fallback when a
// glTF asset omits TANGENT. Output is packed into Vertex3D::tangent as
// (Tx, Ty, Tz, bitangent_sign) per glTF spec. Not MikkTSpace-correct on UV
// seams, but fine for v1.
void compute_tangents_from_uvs(std::vector<gpu::Vertex3D>& vertices,
                                const std::vector<uint32_t>& indices);

} // namespace mmo::engine
