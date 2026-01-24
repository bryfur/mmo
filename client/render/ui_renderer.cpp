#include "ui_renderer.hpp"
#include "text_renderer.hpp"
#include <glm/gtc/matrix_transform.hpp>
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
    
    // UI shader
    ui_shader_ = std::make_unique<Shader>();
    if (!ui_shader_->load(shaders::ui_vertex, shaders::ui_fragment)) {
        std::cerr << "Failed to load UI shader" << std::endl;
        return false;
    }
    
    // Text shader
    text_shader_ = std::make_unique<Shader>();
    if (!text_shader_->load(shaders::text_vertex, shaders::text_fragment)) {
        std::cerr << "Failed to load text shader" << std::endl;
        return false;
    }
    
    // UI VAO/VBO
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, 6 * 6 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    
    // Position (2D)
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    // Color
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
    
    glBindVertexArray(0);
    
    // Text VAO/VBO
    glGenVertexArrays(1, &text_vao_);
    glGenBuffers(1, &text_vbo_);
    
    glBindVertexArray(text_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, text_vbo_);
    glBufferData(GL_ARRAY_BUFFER, 6 * 4 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    
    glBindVertexArray(0);
    
    // Initialize text renderer
    text_renderer_ = std::make_unique<TextRenderer>();
    if (text_renderer_->init()) {
        text_renderer_->set_shader(text_shader_.get());
        text_renderer_->set_vao_vbo(text_vao_, text_vbo_);
        std::cout << "UI text renderer initialized" << std::endl;
    } else {
        std::cerr << "Failed to initialize text renderer" << std::endl;
    }
    
    // Set up projection
    set_screen_size(width, height);
    
    return true;
}

void UIRenderer::shutdown() {
    if (vao_) {
        glDeleteVertexArrays(1, &vao_);
        vao_ = 0;
    }
    if (vbo_) {
        glDeleteBuffers(1, &vbo_);
        vbo_ = 0;
    }
    if (text_vao_) {
        glDeleteVertexArrays(1, &text_vao_);
        text_vao_ = 0;
    }
    if (text_vbo_) {
        glDeleteBuffers(1, &text_vbo_);
        text_vbo_ = 0;
    }
    
    if (text_renderer_) {
        text_renderer_->shutdown();
        text_renderer_.reset();
    }
    
    ui_shader_.reset();
    text_shader_.reset();
}

void UIRenderer::set_screen_size(int width, int height) {
    width_ = width;
    height_ = height;
    projection_ = glm::ortho(0.0f, static_cast<float>(width), 
                              static_cast<float>(height), 0.0f, -1.0f, 1.0f);
}

void UIRenderer::begin() {
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    
    ui_shader_->use();
    ui_shader_->set_mat4("projection", projection_);
    
    if (text_renderer_ && text_renderer_->is_ready()) {
        text_renderer_->set_projection(projection_);
    }
}

void UIRenderer::end() {
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
}

glm::vec4 UIRenderer::color_from_uint32(uint32_t color) const {
    float r = (color & 0xFF) / 255.0f;
    float g = ((color >> 8) & 0xFF) / 255.0f;
    float b = ((color >> 16) & 0xFF) / 255.0f;
    float a = ((color >> 24) & 0xFF) / 255.0f;
    return glm::vec4(r, g, b, a);
}

void UIRenderer::draw_quad(float x, float y, float w, float h, const glm::vec4& color) {
    float vertices[] = {
        x, y,         color.r, color.g, color.b, color.a,
        x + w, y,     color.r, color.g, color.b, color.a,
        x + w, y + h, color.r, color.g, color.b, color.a,
        x, y,         color.r, color.g, color.b, color.a,
        x + w, y + h, color.r, color.g, color.b, color.a,
        x, y + h,     color.r, color.g, color.b, color.a,
    };
    
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}

void UIRenderer::draw_filled_rect(float x, float y, float w, float h, uint32_t color) {
    draw_quad(x, y, w, h, color_from_uint32(color));
}

void UIRenderer::draw_rect_outline(float x, float y, float w, float h, uint32_t color, float line_width) {
    glm::vec4 c = color_from_uint32(color);
    draw_quad(x, y, w, line_width, c);  // Top
    draw_quad(x, y + h - line_width, w, line_width, c);  // Bottom
    draw_quad(x, y, line_width, h, c);  // Left
    draw_quad(x + w - line_width, y, line_width, h, c);  // Right
}

void UIRenderer::draw_circle(float x, float y, float radius, uint32_t color, int segments) {
    glm::vec4 c = color_from_uint32(color);
    
    std::vector<float> circle_data;
    for (int i = 0; i < segments; ++i) {
        float a1 = (i / static_cast<float>(segments)) * 2.0f * 3.14159f;
        float a2 = ((i + 1) / static_cast<float>(segments)) * 2.0f * 3.14159f;
        
        circle_data.insert(circle_data.end(), {x, y, c.r, c.g, c.b, c.a});
        circle_data.insert(circle_data.end(), {x + std::cos(a1) * radius, y + std::sin(a1) * radius, c.r, c.g, c.b, c.a});
        circle_data.insert(circle_data.end(), {x + std::cos(a2) * radius, y + std::sin(a2) * radius, c.r, c.g, c.b, c.a});
    }
    
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, circle_data.size() * sizeof(float), circle_data.data(), GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, segments * 3);
    glBindVertexArray(0);
}

void UIRenderer::draw_circle_outline(float x, float y, float radius, uint32_t color, 
                                      float line_width, int segments) {
    draw_circle(x, y, radius, color, segments);
}

void UIRenderer::draw_line(float x1, float y1, float x2, float y2, uint32_t color, float line_width) {
    glm::vec4 c = color_from_uint32(color);
    
    float dx = x2 - x1;
    float dy = y2 - y1;
    float len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.001f) return;
    
    float nx = -dy / len * line_width / 2;
    float ny = dx / len * line_width / 2;
    
    float vertices[] = {
        x1 + nx, y1 + ny, c.r, c.g, c.b, c.a,
        x1 - nx, y1 - ny, c.r, c.g, c.b, c.a,
        x2 - nx, y2 - ny, c.r, c.g, c.b, c.a,
        x1 + nx, y1 + ny, c.r, c.g, c.b, c.a,
        x2 - nx, y2 - ny, c.r, c.g, c.b, c.a,
        x2 + nx, y2 + ny, c.r, c.g, c.b, c.a,
    };
    
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}

void UIRenderer::draw_text(const std::string& text, float x, float y, uint32_t color, float scale) {
    if (text_renderer_ && text_renderer_->is_ready()) {
        text_renderer_->set_projection(projection_);
        text_renderer_->draw_text(text, x, y, color, scale);
        // Restore UI shader
        ui_shader_->use();
        ui_shader_->set_mat4("projection", projection_);
    }
}

void UIRenderer::draw_button(float x, float y, float w, float h, const std::string& label, 
                              uint32_t color, bool selected) {
    draw_filled_rect(x, y, w, h, color);
    uint32_t border_color = selected ? 0xFFFFFFFF : 0xFF888888;
    draw_rect_outline(x, y, w, h, border_color, selected ? 3.0f : 2.0f);
    
    if (text_renderer_ && text_renderer_->is_ready() && !label.empty()) {
        text_renderer_->set_projection(projection_);
        int text_w = text_renderer_->get_text_width(label, 1.0f);
        int text_h = text_renderer_->get_text_height(1.0f);
        float text_x = x + (w - text_w) / 2.0f;
        float text_y = y + (h - text_h) / 2.0f;
        text_renderer_->draw_text(label, text_x, text_y, 0xFFFFFFFF, 1.0f);
        ui_shader_->use();
        ui_shader_->set_mat4("projection", projection_);
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
