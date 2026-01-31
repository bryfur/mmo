#pragma once

#include <glm/glm.hpp>
#include <cstdint>

namespace mmo {

// ============================================================================
// Fog settings
// ============================================================================

namespace fog {
    // Default fog (entities, terrain)
    constexpr glm::vec3 COLOR = {0.35f, 0.45f, 0.6f};
    constexpr float START = 1500.0f;
    constexpr float END = 6000.0f;

    // Distant objects (mountains)
    constexpr glm::vec3 DISTANT_COLOR = {0.55f, 0.55f, 0.6f};
    constexpr float DISTANT_START = 3000.0f;
    constexpr float DISTANT_END = 12000.0f;
}

// ============================================================================
// Lighting defaults
// ============================================================================

namespace lighting {
    constexpr glm::vec3 SUN_DIRECTION = {0.5f, 0.8f, 0.3f};
    constexpr glm::vec3 LIGHT_DIR = {-0.5f, -0.8f, -0.3f};
    constexpr glm::vec3 LIGHT_COLOR = {1.0f, 0.95f, 0.9f};
    constexpr glm::vec3 AMBIENT_COLOR = {0.4f, 0.4f, 0.5f};
    constexpr glm::vec3 AMBIENT_COLOR_NO_FOG = {0.5f, 0.5f, 0.55f};
}

// ============================================================================
// Player class colors (ARGB format: 0xAARRGGBB)
// ============================================================================

namespace class_colors {
    constexpr uint32_t WARRIOR = 0xFFC85050;
    constexpr uint32_t MAGE    = 0xFF5050C8;
    constexpr uint32_t PALADIN = 0xFFC8B450;
    constexpr uint32_t ARCHER  = 0xFF50C850;
    constexpr uint32_t DEFAULT = 0xFFFFFFFF;
}

// ============================================================================
// UI colors
// ============================================================================

namespace ui_colors {
    constexpr uint32_t WHITE       = 0xFFFFFFFF;
    constexpr uint32_t TEXT_DIM    = 0xFFAAAAAA;
    constexpr uint32_t TEXT_HINT   = 0xFF888888;
    constexpr uint32_t PANEL_BG    = 0xCC222222;
    constexpr uint32_t MENU_BG     = 0xE0222222;
    constexpr uint32_t TITLE_BG    = 0xFF332211;
    constexpr uint32_t SELECTION   = 0x40FFFFFF;
    constexpr uint32_t BORDER      = 0xFF666666;
    constexpr uint32_t BORDER_DIM  = 0xFF888888;

    // Health bar
    constexpr uint32_t HEALTH_HIGH    = 0xFF00CC00;
    constexpr uint32_t HEALTH_MED     = 0xFF00CCCC;
    constexpr uint32_t HEALTH_LOW     = 0xFF0000CC;
    constexpr uint32_t HEALTH_BG      = 0xFF000066;
    constexpr uint32_t HEALTH_FRAME   = 0xFF000000;
    constexpr uint32_t HEALTH_BAR_BG  = 0xE6000066;
    constexpr uint32_t HEALTH_3D_BG   = 0xCC000000;

    // Menu values
    constexpr uint32_t VALUE_ON   = 0xFF00FF00;
    constexpr uint32_t VALUE_OFF  = 0xFFFF6666;
    constexpr uint32_t VALUE_SLIDER = 0xFF00AAFF;
    constexpr uint32_t FPS_TEXT   = 0xFF00FF00;

    // Reticle
    constexpr uint32_t RETICLE = 0xCCFFFFFF;
}

// ============================================================================
// Class select UI colors (background tiles, different from class_colors)
// ============================================================================

namespace class_select_colors {
    constexpr uint32_t WARRIOR = 0xFF5050C8;
    constexpr uint32_t MAGE    = 0xFFC85050;
    constexpr uint32_t PALADIN = 0xFF50B4C8;
    constexpr uint32_t ARCHER  = 0xFF50C850;
}

} // namespace mmo
