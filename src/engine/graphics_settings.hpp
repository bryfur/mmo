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

    // Quality settings
    int anisotropic_filter = 4; // 0=off, 1=2x, 2=4x, 3=8x, 4=16x
    int vsync_mode = 1;      // 0=off, 1=double buffer (vsync), 2=triple buffer
};

} // namespace mmo::engine
