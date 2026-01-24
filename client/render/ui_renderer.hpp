#pragma once

#include "../shader.hpp"
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <memory>
#include <string>

namespace mmo {

class TextRenderer;

/**
 * UIRenderer handles all 2D UI rendering:
 * - Rectangles (filled/outline)
 * - Circles
 * - Lines
 * - Text
 * - Buttons
 * - Health bars
 */
class UIRenderer {
public:
    UIRenderer();
    ~UIRenderer();
    
    // Non-copyable
    UIRenderer(const UIRenderer&) = delete;
    UIRenderer& operator=(const UIRenderer&) = delete;
    
    /**
     * Initialize UI rendering resources.
     */
    bool init(int width, int height);
    
    /**
     * Clean up resources.
     */
    void shutdown();
    
    /**
     * Update screen dimensions (call on resize).
     */
    void set_screen_size(int width, int height);
    
    /**
     * Begin UI rendering mode (disables depth test, culling).
     */
    void begin();
    
    /**
     * End UI rendering mode (restores state).
     */
    void end();
    
    // Primitive drawing
    void draw_filled_rect(float x, float y, float w, float h, uint32_t color);
    void draw_rect_outline(float x, float y, float w, float h, uint32_t color, float line_width = 2.0f);
    void draw_circle(float x, float y, float radius, uint32_t color, int segments = 24);
    void draw_circle_outline(float x, float y, float radius, uint32_t color, 
                             float line_width = 2.0f, int segments = 24);
    void draw_line(float x1, float y1, float x2, float y2, uint32_t color, float line_width = 2.0f);
    
    // Text drawing
    void draw_text(const std::string& text, float x, float y, uint32_t color, float scale = 1.0f);
    
    // Composite UI elements
    void draw_button(float x, float y, float w, float h, const std::string& label, 
                     uint32_t color, bool selected);
    void draw_player_health_bar(float health_ratio, float max_health, int screen_width, int screen_height);
    void draw_target_reticle(int screen_width, int screen_height);
    
    // Access text renderer for advanced usage
    TextRenderer* text_renderer() { return text_renderer_.get(); }
    
    int width() const { return width_; }
    int height() const { return height_; }
    
private:
    void draw_quad(float x, float y, float w, float h, const glm::vec4& color);
    glm::vec4 color_from_uint32(uint32_t color) const;
    
    int width_ = 0;
    int height_ = 0;
    
    std::unique_ptr<Shader> ui_shader_;
    std::unique_ptr<TextRenderer> text_renderer_;
    std::unique_ptr<Shader> text_shader_;
    
    glm::mat4 projection_;
    
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    
    GLuint text_vao_ = 0;
    GLuint text_vbo_ = 0;
};

} // namespace mmo
