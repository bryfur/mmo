#pragma once

#include <entt/entt.hpp>

namespace mmo::client {

class NetworkSmoother {
public:
    void update(entt::registry& registry, float dt);

    // Configure interpolation behavior
    void set_interpolation_time(float time) { interpolation_time_ = time; }

private:
    // Time in seconds to interpolate between server snapshots
    // Should roughly match server tick interval for smooth movement
    float interpolation_time_ = 1.0f / 60.0f;  // Default, updated from server config
};

} // namespace mmo::client
