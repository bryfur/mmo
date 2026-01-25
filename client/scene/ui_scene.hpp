#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <variant>

namespace mmo {

/**
 * Filled rectangle command
 */
struct FilledRectCommand {
    float x, y, w, h;
    uint32_t color;
};

/**
 * Rectangle outline command
 */
struct RectOutlineCommand {
    float x, y, w, h;
    uint32_t color;
    float line_width = 2.0f;
};

/**
 * Circle command
 */
struct CircleCommand {
    float x, y, radius;
    uint32_t color;
    int segments = 24;
};

/**
 * Circle outline command
 */
struct CircleOutlineCommand {
    float x, y, radius;
    uint32_t color;
    float line_width = 2.0f;
    int segments = 24;
};

/**
 * Line command
 */
struct LineCommand {
    float x1, y1, x2, y2;
    uint32_t color;
    float line_width = 2.0f;
};

/**
 * Text render command
 */
struct TextCommand {
    std::string text;
    float x, y;
    float scale = 1.0f;
    uint32_t color = 0xFFFFFFFF;
};

/**
 * Button render command
 */
struct ButtonCommand {
    float x, y, w, h;
    std::string label;
    uint32_t color;
    bool selected = false;
};

/**
 * Target reticle marker command
 */
struct TargetReticleCommand {};

/**
 * Player health bar (UI overlay)
 */
struct PlayerHealthBarCommand {
    float health_ratio;
    float max_health;
};

/**
 * Enemy health bar in 3D space
 */
struct EnemyHealthBar3DCommand {
    float world_x, world_y, world_z;
    float width;
    float health_ratio;
};

/**
 * Generic UI command using std::variant for type-safe storage
 * Only stores one command type at a time for memory efficiency
 */
using UICommandData = std::variant<
    FilledRectCommand,
    RectOutlineCommand,
    CircleCommand,
    CircleOutlineCommand,
    LineCommand,
    TextCommand,
    ButtonCommand,
    TargetReticleCommand,
    PlayerHealthBarCommand,
    EnemyHealthBar3DCommand
>;

struct UICommand {
    UICommandData data;
    
    // Helper methods for type checking
    template<typename T>
    bool is() const { return std::holds_alternative<T>(data); }
    
    template<typename T>
    const T& get() const { return std::get<T>(data); }
    
    template<typename T>
    T& get() { return std::get<T>(data); }
};

/**
 * UIScene collects all 2D UI render commands.
 * Game logic populates this, then the Renderer consumes it to draw.
 * 
 * Benefits:
 * - Decouples UI data from rendering
 * - Enables UI batching and optimization
 * - Makes UI rendering testable
 */
class UIScene {
public:
    UIScene() = default;
    ~UIScene() = default;
    
    /**
     * Clear all UI commands. Call at start of each frame.
     */
    void clear();
    
    // ========== Shape Commands ==========
    
    void add_filled_rect(float x, float y, float w, float h, uint32_t color);
    void add_rect_outline(float x, float y, float w, float h, uint32_t color, float line_width = 2.0f);
    void add_circle(float x, float y, float radius, uint32_t color, int segments = 24);
    void add_circle_outline(float x, float y, float radius, uint32_t color, 
                            float line_width = 2.0f, int segments = 24);
    void add_line(float x1, float y1, float x2, float y2, uint32_t color, float line_width = 2.0f);
    
    // ========== Text Commands ==========
    
    void add_text(const std::string& text, float x, float y, float scale = 1.0f, uint32_t color = 0xFFFFFFFF);
    
    // ========== Widget Commands ==========
    
    void add_button(float x, float y, float w, float h, const std::string& label,
                    uint32_t color, bool selected = false);
    
    // ========== Special UI Elements ==========
    
    void add_target_reticle();
    void add_player_health_bar(float health_ratio, float max_health);
    void add_enemy_health_bar_3d(float world_x, float world_y, float world_z,
                                  float width, float health_ratio);
    
    // ========== Command Access ==========
    
    const std::vector<UICommand>& commands() const { return commands_; }
    bool has_target_reticle() const { return has_target_reticle_; }
    
private:
    std::vector<UICommand> commands_;
    bool has_target_reticle_ = false;
};

} // namespace mmo
