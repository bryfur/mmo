#include "client/hud/hud_layout.hpp"

#include <algorithm>
#include <cmath>

namespace mmo::client::hud_layout {

MinimapPoint world_to_minimap(const MinimapView& view, float world_x, float world_z) {
    MinimapPoint out{};
    if (view.world_radius <= 0.0f) {
        // Degenerate config — pin to center, mark out-of-bounds.
        out.x = view.center_x;
        out.y = view.center_y;
        out.in_bounds = false;
        return out;
    }

    const float dx = world_x - view.player_world_x;
    const float dz = world_z - view.player_world_z;
    const float scale = view.map_radius / view.world_radius;
    const float map_dx = dx * scale;
    const float map_dz = dz * scale;

    out.x = view.center_x + map_dx;
    out.y = view.center_y + map_dz;

    const float dist = std::sqrt(map_dx * map_dx + map_dz * map_dz);
    out.in_bounds = dist < (view.map_radius - view.bound_inset);
    return out;
}

float minimap_area_pixel_radius(float world_radius_units, float minimap_world_radius, float minimap_pixel_radius,
                                float min_pixels) {
    if (minimap_world_radius <= 0.0f) {
        return min_pixels;
    }
    const float r = (world_radius_units / minimap_world_radius) * minimap_pixel_radius;
    const float max_r = minimap_pixel_radius * 0.5f;
    return std::max(min_pixels, std::min(r, max_r));
}

float bar_ratio(float current, float max) {
    if (max <= 0.0f) {
        return 0.0f;
    }
    const float r = current / max;
    if (r < 0.0f) {
        return 0.0f;
    }
    if (r > 1.0f) {
        return 1.0f;
    }
    return r;
}

uint32_t health_bar_color(float ratio) {
    // Color wheel: red below 30%, orange below 60%, green otherwise.
    if (ratio < 0.3f) {
        return 0xFF0000CC; // red
    }
    if (ratio < 0.6f) {
        return 0xFF00AAFF; // orange
    }
    return 0xFF0000FF; // green
}

uint32_t fade_color(uint32_t color, float alpha) {
    if (alpha >= 1.0f) {
        return color;
    }
    if (alpha <= 0.0f) {
        return color & 0x00FFFFFFu;
    }
    const uint32_t a = static_cast<uint32_t>(((color >> 24) & 0xFFu) * alpha);
    return (a << 24) | (color & 0x00FFFFFFu);
}

float linear_fade(float timer, float fade_duration) {
    if (timer <= 0.0f) {
        return 0.0f;
    }
    if (fade_duration <= 0.0f) {
        return timer > 0.0f ? 1.0f : 0.0f;
    }
    if (timer >= fade_duration) {
        return 1.0f;
    }
    return timer / fade_duration;
}

std::string truncate_with_ellipsis(std::string_view text, std::size_t max_chars, char ellipsis) {
    if (text.size() <= max_chars) {
        return std::string(text);
    }
    if (max_chars == 0) {
        return std::string{};
    }
    std::string out;
    out.reserve(max_chars);
    out.assign(text.substr(0, max_chars - 1));
    out.push_back(ellipsis);
    return out;
}

} // namespace mmo::client::hud_layout
