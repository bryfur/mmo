#pragma once

#include <entt/entt.hpp>

namespace mmo::client {

namespace ecs {
struct Transform;
struct Interpolation;
} // namespace ecs

// Pure interpolation step. Advances `interp.alpha` by dt/interpolation_time and
// writes the smoothstep-eased blend into `out_x/y/z`. When alpha clamps to 1
// the previous-position fields snap to the target so subsequent calls with the
// same target are no-ops. Extracted from update() so it can be unit tested
// without spinning up an entt::registry.
void smooth_step(ecs::Transform& transform, ecs::Interpolation& interp, float dt, float interpolation_time);

class NetworkSmoother {
public:
    void update(entt::registry& registry, float dt);

    void set_interpolation_time(float time) { interpolation_time_ = time; }
    float interpolation_time() const { return interpolation_time_; }

private:
    // Time in seconds to interpolate between server snapshots.
    // Should roughly match server tick interval for smooth movement.
    float interpolation_time_ = 1.0f / 60.0f;
};

} // namespace mmo::client
