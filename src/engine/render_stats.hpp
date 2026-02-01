#pragma once

#include <cstdint>

namespace mmo::engine {

struct RenderStats {
    uint32_t draw_calls = 0;
    uint32_t triangle_count = 0;
    uint32_t entities_rendered = 0;
    uint32_t entities_distance_culled = 0;
    uint32_t entities_frustum_culled = 0;
};

} // namespace mmo::engine
