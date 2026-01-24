#pragma once

#include "common/ecs/components.hpp"
#include "common/protocol.hpp"
#include <entt/entt.hpp>

namespace mmo {

class InterpolationSystem {
public:
    void update(entt::registry& registry, float dt);
    
    // Configure interpolation behavior
    void set_interpolation_time(float time) { interpolation_time_ = time; }
    
private:
    // Time in seconds to interpolate between server snapshots
    // Should roughly match server tick interval for smooth movement
    float interpolation_time_ = config::TICK_DURATION;
};

} // namespace mmo
