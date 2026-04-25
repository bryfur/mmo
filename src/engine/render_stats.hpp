#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace mmo::engine {

struct RenderStats {
    uint32_t draw_calls = 0;
    uint32_t triangle_count = 0;
    uint32_t entities_rendered = 0;
    uint32_t entities_distance_culled = 0;
    uint32_t entities_frustum_culled = 0;
    uint32_t jobs_pending = 0;
    std::vector<std::pair<std::string, float>> pass_times_ms;
};

} // namespace mmo::engine
