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
    bool show_debug_hud = false;

    // Culling settings
    int draw_distance = 3;    // 0=500, 1=1000, 2=2000, 3=4000, 4=8000
    bool frustum_culling = true;

    // Quality settings
    int anisotropic_filter = 4; // 0=off, 1=2x, 2=4x, 3=8x, 4=16x
    int vsync_mode = 0;      // 0=immediate, 1=vsync, 2=mailbox
    int shadow_mode = 2;     // 0=off, 1=hard, 2=PCSS
    int shadow_cascades = 1; // 0=1, 1=2, 2=3, 3=4
    int shadow_resolution = 2; // 0=512, 1=1024, 2=2048, 3=4096
    int ao_mode = 1;         // 0=off, 1=SSAO, 2=GTAO
    int window_mode = 0;     // 0=windowed, 1=borderless fullscreen, 2=exclusive fullscreen
    int resolution_index = 0; // index into available native display modes

    float get_draw_distance() const {
        constexpr float distances[] = {500.0f, 1000.0f, 2000.0f, 4000.0f, 8000.0f};
        return distances[draw_distance];
    }
};

} // namespace mmo::engine
