#include "interpolation_system.hpp"
#include <cmath>
#include <algorithm>

namespace mmo {

void InterpolationSystem::update(entt::registry& registry, float dt) {
    // Interpolate entity positions for smooth rendering
    // This smooths out network jitter by interpolating between
    // the previous position and the target server position
    
    auto view = registry.view<ecs::Transform, ecs::Interpolation>();
    
    for (auto entity : view) {
        auto& transform = view.get<ecs::Transform>(entity);
        auto& interp = view.get<ecs::Interpolation>(entity);
        
        // Advance interpolation progress based on time
        // alpha goes from 0 to 1 over interpolation_time_
        interp.alpha += dt / interpolation_time_;
        interp.alpha = std::min(interp.alpha, 1.0f);
        
        // Smoothstep for more natural easing (ease-in-out)
        // t = 3t² - 2t³
        float t = interp.alpha;
        float smooth_t = t * t * (3.0f - 2.0f * t);
        
        // Lerp between previous position and target
        transform.x = interp.prev_x + (interp.target_x - interp.prev_x) * smooth_t;
        transform.y = interp.prev_y + (interp.target_y - interp.prev_y) * smooth_t;
        
        // If we've fully caught up, snap to target
        // This prevents floating point drift
        if (interp.alpha >= 1.0f) {
            transform.x = interp.target_x;
            transform.y = interp.target_y;
            interp.prev_x = interp.target_x;
            interp.prev_y = interp.target_y;
        }
    }
}

} // namespace mmo
