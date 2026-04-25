#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace mmo::client::hud_layout {

// ---------------------------------------------------------------------------
// Pure layout / math helpers shared by HUD widgets. Extracted from inline
// lambdas + duplicated rules so they can be unit tested without touching
// rendering or game state.
// ---------------------------------------------------------------------------

struct MinimapPoint {
    float x = 0.0f;
    float y = 0.0f;
    bool in_bounds = false;
};

struct MinimapView {
    float center_x = 0.0f; // screen-space center of the minimap
    float center_y = 0.0f;
    float map_radius = 0.0f;     // pixel radius of the visible disc
    float world_radius = 0.0f;   // world units mapped to map_radius
    float player_world_x = 0.0f; // viewer position in world space
    float player_world_z = 0.0f;
    float bound_inset = 3.0f; // pixels inside the disc that count as "in bounds"
};

// Project a world-space point onto the minimap. `in_bounds` is true when the
// projected point sits inside (map_radius - bound_inset).
MinimapPoint world_to_minimap(const MinimapView& view, float world_x, float world_z);

// Pixel size of an objective area on the minimap, clamped so very small or
// very large radii still render usefully.
float minimap_area_pixel_radius(float world_radius_units, float minimap_world_radius, float minimap_pixel_radius,
                                float min_pixels = 4.0f);

// Clamp ratio to [0, 1] handling division by zero. Used by every progress bar.
float bar_ratio(float current, float max);

// Health-bar fill color by ratio. Fixed palette (green / orange / red). Pure
// function so the threshold rules are testable without rendering.
uint32_t health_bar_color(float ratio);

// Apply alpha to an ABGR color by scaling the high byte. Equivalent to the
// fade_color() helper that lived in gameplay_hud.cpp.
uint32_t fade_color(uint32_t color, float alpha);

// Compute fade alpha for a timer that lingers fully opaque, then linearly
// fades to zero over `fade_duration` once `timer` drops below it. Used by
// zone-name, level-up, and loot-feed widgets.
float linear_fade(float timer, float fade_duration);

// Truncate `text` to `max_chars` glyphs, replacing the final glyph with a
// single-character ellipsis (default '~') when truncated. Returns text
// unchanged if it already fits.
std::string truncate_with_ellipsis(std::string_view text, std::size_t max_chars, char ellipsis = '~');

} // namespace mmo::client::hud_layout
