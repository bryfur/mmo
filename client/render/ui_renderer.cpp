#include "ui_renderer.hpp"
#include "text_renderer.hpp"
#include "bgfx_utils.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <vector>
#include <cmath>
#include <iostream>
#include <cstdio>

namespace mmo {

UIRenderer::UIRenderer() = default;

UIRenderer::~UIRenderer() {
    shutdown();
}

bool UIRenderer::init(int width, int height) {
    width_ = width;
    height_ = height;
    
    // Set up vertex layout for UI (position 2D + color RGBA)
    ui_layout_
        .begin()
        .add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)  // Normalized
        .end();
    
    // Load UI shader
    ui_program_ = bgfx_utils::load_program("ui_vs", "ui_fs");
    if (!bgfx::isValid(ui_program_)) {
        std::cerr << "Failed to load UI shader program" << std::endl;
        return false;
    }
    
    // Create uniform for projection matrix
    u_projection_ = bgfx::createUniform("u_projection", bgfx::UniformType::Mat4);
    
    // Initialize text renderer
    text_renderer_ = std::make_unique<TextRenderer>();
    if (text_renderer_->init(width, height)) {
        std::cout << "UI text renderer initialized" << std::endl;
    } else {
        std::cerr << "Warning: Failed to initialize text renderer" << std::endl;
    }
    
    // Set up projection
    set_screen_size(width, height);
    
    std::cout << "UIRenderer initialized (bgfx)" << std::endl;
    return true;
}

void UIRenderer::shutdown() {
    if (bgfx::isValid(ui_program_)) {
        bgfx::destroy(ui_program_);
        ui_program_ = BGFX_INVALID_HANDLE;
    }
    
    if (bgfx::isValid(u_projection_)) {
        bgfx::destroy(u_projection_);
        u_projection_ = BGFX_INVALID_HANDLE;
    }
    
    if (text_renderer_) {
        text_renderer_->shutdown();
        text_renderer_.reset();
    }
}

void UIRenderer::set_screen_size(int width, int height) {
    width_ = width;
    height_ = height;
    projection_ = glm::ortho(0.0f, static_cast<float>(width), 
                              static_cast<float>(height), 0.0f, -1.0f, 1.0f);
    
    if (text_renderer_) {
        text_renderer_->set_screen_size(width, height);
    }
}

void UIRenderer::begin() {
    // Set up UI view
    bgfx::setViewRect(ViewId::UI, 0, 0, uint16_t(width_), uint16_t(height_));
    
    // Set view transform with orthographic projection
    float view[16];
    float proj[16];
    
    glm::mat4 identity(1.0f);
    memcpy(view, glm::value_ptr(identity), sizeof(view));
    memcpy(proj, glm::value_ptr(projection_), sizeof(proj));
    
    bgfx::setViewTransform(ViewId::UI, view, proj);
}

void UIRenderer::end() {
    // Nothing special needed for bgfx
}

uint32_t UIRenderer::rgba_to_abgr(uint32_t color) const {
    // Input: ABGR (alpha in high byte)
    // Output: ABGR (same format, but we need to handle vertex color format)
    // bgfx expects ABGR in vertex colors when using normalized uint8
    uint8_t r = color & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = (color >> 16) & 0xFF;
    uint8_t a = (color >> 24) & 0xFF;
    return (a << 24) | (b << 16) | (g << 8) | r;  // ABGR format
}

void UIRenderer::draw_quad(float x, float y, float w, float h, uint32_t abgr_color) {
    // Use transient vertex buffer for dynamic UI
    if (bgfx::getAvailTransientVertexBuffer(6, ui_layout_) < 6) {
        return;  // Not enough space
    }
    
    bgfx::TransientVertexBuffer tvb;
    bgfx::allocTransientVertexBuffer(&tvb, 6, ui_layout_);
    
    struct UIVertex {
        float x, y;
        uint32_t color;
    };
    
    UIVertex* vertices = (UIVertex*)tvb.data;
    
    // Two triangles for quad
    vertices[0] = {x, y, abgr_color};
    vertices[1] = {x + w, y, abgr_color};
    vertices[2] = {x + w, y + h, abgr_color};
    vertices[3] = {x, y, abgr_color};
    vertices[4] = {x + w, y + h, abgr_color};
    vertices[5] = {x, y + h, abgr_color};
    
    bgfx::setVertexBuffer(0, &tvb);
    
    // Set state: alpha blending, no depth test
    uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
                   | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA);
    bgfx::setState(state);
    
    bgfx::submit(ViewId::UI, ui_program_);
}

void UIRenderer::draw_filled_rect(float x, float y, float w, float h, uint32_t color) {
    draw_quad(x, y, w, h, rgba_to_abgr(color));
}

void UIRenderer::draw_rect_outline(float x, float y, float w, float h, uint32_t color, float line_width) {
    uint32_t c = rgba_to_abgr(color);
    draw_quad(x, y, w, line_width, c);  // Top
    draw_quad(x, y + h - line_width, w, line_width, c);  // Bottom
    draw_quad(x, y, line_width, h, c);  // Left
    draw_quad(x + w - line_width, y, line_width, h, c);  // Right
}

void UIRenderer::draw_circle(float x, float y, float radius, uint32_t color, int segments) {
    uint32_t c = rgba_to_abgr(color);
    
    int num_vertices = segments * 3;
    if (bgfx::getAvailTransientVertexBuffer(num_vertices, ui_layout_) < (uint32_t)num_vertices) {
        return;
    }
    
    bgfx::TransientVertexBuffer tvb;
    bgfx::allocTransientVertexBuffer(&tvb, num_vertices, ui_layout_);
    
    struct UIVertex {
        float x, y;
        uint32_t color;
    };
    
    UIVertex* vertices = (UIVertex*)tvb.data;
    
    for (int i = 0; i < segments; ++i) {
        float a1 = (i / static_cast<float>(segments)) * 2.0f * 3.14159f;
        float a2 = ((i + 1) / static_cast<float>(segments)) * 2.0f * 3.14159f;
        
        vertices[i * 3 + 0] = {x, y, c};
        vertices[i * 3 + 1] = {x + std::cos(a1) * radius, y + std::sin(a1) * radius, c};
        vertices[i * 3 + 2] = {x + std::cos(a2) * radius, y + std::sin(a2) * radius, c};
    }
    
    bgfx::setVertexBuffer(0, &tvb);
    
    uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
                   | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA);
    bgfx::setState(state);
    
    bgfx::submit(ViewId::UI, ui_program_);
}

void UIRenderer::draw_circle_outline(float x, float y, float radius, uint32_t color, 
                                      float line_width, int segments) {
    // Draw as filled circle (simplification - could do proper outline later)
    draw_circle(x, y, radius, color, segments);
}

void UIRenderer::draw_line(float x1, float y1, float x2, float y2, uint32_t color, float line_width) {
    uint32_t c = rgba_to_abgr(color);
    
    float dx = x2 - x1;
    float dy = y2 - y1;
    float len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.001f) return;
    
    float nx = -dy / len * line_width / 2;
    float ny = dx / len * line_width / 2;
    
    if (bgfx::getAvailTransientVertexBuffer(6, ui_layout_) < 6) {
        return;
    }
    
    bgfx::TransientVertexBuffer tvb;
    bgfx::allocTransientVertexBuffer(&tvb, 6, ui_layout_);
    
    struct UIVertex {
        float x, y;
        uint32_t color;
    };
    
    UIVertex* vertices = (UIVertex*)tvb.data;
    
    vertices[0] = {x1 + nx, y1 + ny, c};
    vertices[1] = {x1 - nx, y1 - ny, c};
    vertices[2] = {x2 - nx, y2 - ny, c};
    vertices[3] = {x1 + nx, y1 + ny, c};
    vertices[4] = {x2 - nx, y2 - ny, c};
    vertices[5] = {x2 + nx, y2 + ny, c};
    
    bgfx::setVertexBuffer(0, &tvb);
    
    uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
                   | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA);
    bgfx::setState(state);
    
    bgfx::submit(ViewId::UI, ui_program_);
}

void UIRenderer::draw_text(const std::string& text, float x, float y, uint32_t color, float scale) {
    if (text_renderer_ && text_renderer_->is_ready()) {
        text_renderer_->draw_text(text, x, y, color, scale);
    }
}

void UIRenderer::draw_button(float x, float y, float w, float h, const std::string& label, 
                              uint32_t color, bool selected) {
    draw_filled_rect(x, y, w, h, color);
    uint32_t border_color = selected ? 0xFFFFFFFF : 0xFF888888;
    draw_rect_outline(x, y, w, h, border_color, selected ? 3.0f : 2.0f);
    
    if (text_renderer_ && text_renderer_->is_ready() && !label.empty()) {
        int text_w = text_renderer_->get_text_width(label, 1.0f);
        int text_h = text_renderer_->get_text_height(1.0f);
        float text_x = x + (w - text_w) / 2.0f;
        float text_y = y + (h - text_h) / 2.0f;
        text_renderer_->draw_text(label, text_x, text_y, 0xFFFFFFFF, 1.0f);
    }
}

void UIRenderer::draw_player_health_bar(float health_ratio, float max_health, int screen_width, int screen_height) {
    float bar_width = 250.0f;
    float bar_height = 25.0f;
    float padding = 20.0f;
    float x = padding;
    float y = screen_height - padding - bar_height;
    
    // Background
    draw_filled_rect(x - 2, y - 2, bar_width + 4, bar_height + 4, 0xFF000000);
    draw_rect_outline(x - 2, y - 2, bar_width + 4, bar_height + 4, 0xFF666666, 2.0f);
    draw_filled_rect(x, y, bar_width, bar_height, 0xFF000066);
    
    // Health color
    uint32_t hp_color;
    if (health_ratio > 0.5f) {
        hp_color = 0xFF00CC00;
    } else if (health_ratio > 0.25f) {
        hp_color = 0xFF00CCCC;
    } else {
        hp_color = 0xFF0000CC;
    }
    draw_filled_rect(x, y, bar_width * health_ratio, bar_height, hp_color);
    
    // Health text
    char hp_text[32];
    snprintf(hp_text, sizeof(hp_text), "HP: %.0f / %.0f", health_ratio * max_health, max_health);
    draw_text(hp_text, x + 10, y + 5, 0xFFFFFFFF, 1.0f);
}

void UIRenderer::draw_target_reticle(int screen_width, int screen_height) {
    float center_x = screen_width / 2.0f;
    float center_y = screen_height / 2.0f;
    
    float outer_radius = 12.0f;
    float inner_radius = 4.0f;
    float line_width = 2.0f;
    uint32_t color = 0xCCFFFFFF;
    
    draw_line(center_x, center_y - outer_radius, center_x, center_y - inner_radius, color, line_width);
    draw_line(center_x, center_y + inner_radius, center_x, center_y + outer_radius, color, line_width);
    draw_line(center_x - outer_radius, center_y, center_x - inner_radius, center_y, color, line_width);
    draw_line(center_x + inner_radius, center_y, center_x + outer_radius, center_y, color, line_width);
    
    float dot_size = 2.0f;
    draw_filled_rect(center_x - dot_size/2, center_y - dot_size/2, dot_size, dot_size, color);
}

} // namespace mmo
