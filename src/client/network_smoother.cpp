#include "network_smoother.hpp"
#include "client/ecs/components.hpp"
#include <algorithm>

namespace mmo::client {

void smooth_step(ecs::Transform& transform,
                 ecs::Interpolation& interp,
                 float dt,
                 float interpolation_time) {
    // Guard against zero/negative interpolation_time so a misconfiguration
    // can't divide-by-zero or run alpha backwards.
    if (interpolation_time <= 0.0f) {
        transform.x = interp.target_x;
        transform.y = interp.target_y;
        transform.z = interp.target_z;
        interp.prev_x = interp.target_x;
        interp.prev_y = interp.target_y;
        interp.prev_z = interp.target_z;
        interp.alpha = 1.0f;
        return;
    }

    interp.alpha = std::min(interp.alpha + dt / interpolation_time, 1.0f);

    // Smoothstep easing: t = 3t² - 2t³
    const float t = interp.alpha;
    const float smooth_t = t * t * (3.0f - 2.0f * t);

    transform.x = interp.prev_x + (interp.target_x - interp.prev_x) * smooth_t;
    transform.y = interp.prev_y + (interp.target_y - interp.prev_y) * smooth_t;
    transform.z = interp.prev_z + (interp.target_z - interp.prev_z) * smooth_t;

    // Snap to target on completion to prevent floating-point drift between
    // snapshots and so the next snapshot's prev_* matches the rendered pos.
    if (interp.alpha >= 1.0f) {
        transform.x = interp.target_x;
        transform.y = interp.target_y;
        transform.z = interp.target_z;
        interp.prev_x = interp.target_x;
        interp.prev_y = interp.target_y;
        interp.prev_z = interp.target_z;
    }
}

void NetworkSmoother::update(entt::registry& registry, float dt) {
    auto view = registry.view<ecs::Transform, ecs::Interpolation>();
    for (auto entity : view) {
        auto& interp = view.get<ecs::Interpolation>(entity);
        // Static entities (trees, rocks, buildings) reach alpha == 1 on the first
        // snapshot and never receive another, so skipping them here avoids running
        // smoothstep math on hundreds of entities every frame.
        if (interp.alpha >= 1.0f) continue;
        smooth_step(view.get<ecs::Transform>(entity),
                    interp,
                    dt,
                    interpolation_time_);
    }
}

} // namespace mmo::client
