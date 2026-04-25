#pragma once

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <functional>

namespace mmo::engine {
class ModelManager;
}

namespace mmo {
class AnimationRegistry;
}

namespace mmo::client::systems {

using TerrainHeightFn = std::function<float(float, float)>;

// Smooth-rotate every entity that owns a SmoothRotation component.
// - Players turn toward their AttackDirection (last attack/move command).
// - Other entities turn toward their Velocity vector when moving, otherwise
//   bleed off any residual turn rate.
// Buildings and Environment entity types are skipped (they don't rotate).
void update_rotation_smoothing(entt::registry& registry, float dt);

// Drive every entity's per-instance AnimationInstance one frame:
//   1. Lazy-bind clips and copy the named state-machine config from the
//      registry on first sight.
//   2. Feed gameplay parameters (speed, attacking) into the state machine.
//   3. Advance the AnimationPlayer (clip time, bone matrices, blends).
//   4. Apply procedural attack tilt, optional foot IK, and body lean — all
//      controlled by the per-entity ProceduralConfig.
//
// `get_terrain_height(x, z)` is sampled for foot IK. It must be safe to call
// for any (x, z) — return the world's base height when out-of-range.
//
// Entities beyond `cull_distance` from `camera_pos` skip the full pipeline —
// they will not be drawn this frame, so their pose does not matter. Pass a
// negative cull_distance to disable distance culling.
void update_animations(entt::registry& registry,
                       float dt,
                       engine::ModelManager& models,
                       const AnimationRegistry& animation_registry,
                       const TerrainHeightFn& get_terrain_height,
                       const glm::vec3& camera_pos,
                       float cull_distance);

} // namespace mmo::client::systems
