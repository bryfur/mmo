#pragma once

namespace mmo::engine {

struct GraphicsSettings {
    bool fog_enabled = true;
    bool grass_enabled = true;
    bool skybox_enabled = true;
    bool mountains_enabled = true;
    bool trees_enabled = true;
    bool rocks_enabled = true;
    bool show_fps = false;

    // Culling settings
    int draw_distance = 3;    // 0=500, 1=1000, 2=2000, 3=4000, 4=8000
    bool frustum_culling = true;

    // Quality settings
    int anisotropic_filter = 4; // 0=off, 1=2x, 2=4x, 3=8x, 4=16x
    int vsync_mode = 1;      // 0=off, 1=double buffer (vsync), 2=triple buffer
    int shadow_mode = 2;     // 0=off, 1=hard, 2=PCSS

    float get_draw_distance() const {
        constexpr float distances[] = {500.0f, 1000.0f, 2000.0f, 4000.0f, 8000.0f};
        return distances[draw_distance];
    }
};

} // namespace mmo::engine
