#include "ui_renderer.hpp"
#include "text_renderer.hpp"
#include "../gpu/gpu_device.hpp"
#include "../gpu/gpu_buffer.hpp"
#include "../gpu/gpu_pipeline.hpp"
#include "../gpu/pipeline_registry.hpp"
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

bool UIRenderer::init(gpu::GPUDevice& device, gpu::PipelineRegistry& pipeline_registry, 
                      int width, int height) {
    device_ = &device;
    pipeline_registry_ = &pipeline_registry;
    width_ = width;
    height_ = height;
    
    // Create dynamic vertex buffer for UI quads
    // Each vertex: x, y, r, g, b, a (6 floats)
    vertex_buffer_ = gpu::GPUBuffer::create_dynamic(
        device, 
        gpu::GPUBuffer::Type::Vertex, 
        MAX_VERTICES * sizeof(UIVertex)
    );
    
    if (!vertex_buffer_) {
        std::cerr << "Failed to create UI vertex buffer" << std::endl;
        return false;
    }
    
    // Reserve space for vertex batch
    vertex_batch_.reserve(MAX_VERTICES);
    
    // Initialize text renderer
    text_renderer_ = std::make_unique<TextRenderer>();
    if (text_renderer_->init(device, pipeline_registry)) {
        std::cout << "UI text renderer initialized" << std::endl;
    } else {
        std::cerr << "Failed to initialize text renderer" << std::endl;
    }
    
    // Set up projection
    set_screen_size(width, height);
    
    return true;
}

void UIRenderer::shutdown() {
    vertex_buffer_.reset();
    
    if (text_renderer_) {
        text_renderer_->shutdown();
        text_renderer_.reset();
    }
    
    device_ = nullptr;
    pipeline_registry_ = nullptr;
}

void UIRenderer::set_screen_size(int width, int height) {
    width_ = width;
    height_ = height;
    // Orthographic projection: origin at top-left, Y increases downward
    projection_ = glm::ortho(0.0f, static_cast<float>(width), 
                              static_cast<float>(height), 0.0f, -1.0f, 1.0f);
}

void UIRenderer::begin(SDL_GPUCommandBuffer* cmd, SDL_GPURenderPass* render_pass) {
    current_cmd_ = cmd;
    current_pass_ = render_pass;
    vertex_batch_.clear();
    
    // Bind UI pipeline (handles blend state, no depth test)
    if (pipeline_registry_) {
        auto* ui_pipeline = pipeline_registry_->get_ui_pipeline();
        if (ui_pipeline) {
            ui_pipeline->bind(render_pass);
        }
    }
    
    // Push projection matrix as uniform data
    SDL_PushGPUVertexUniformData(cmd, 0, &projection_, sizeof(glm::mat4));
    
    if (text_renderer_ && text_renderer_->is_ready()) {
        text_renderer_->set_projection(projection_);
    }
}

void UIRenderer::end() {
    // Flush any remaining vertices
    flush_batch();
    
    current_cmd_ = nullptr;
    current_pass_ = nullptr;
}

void UIRenderer::flush_batch() {
    if (vertex_batch_.empty() || !current_cmd_ || !current_pass_ || !vertex_buffer_) {
        return;
    }
    
    // Upload vertex data to GPU
    vertex_buffer_->update(current_cmd_, vertex_batch_.data(), 
                           vertex_batch_.size() * sizeof(UIVertex));
    
    // Bind vertex buffer
    SDL_GPUBufferBinding vb_binding = {};
    vb_binding.buffer = vertex_buffer_->handle();
    vb_binding.offset = 0;
    SDL_BindGPUVertexBuffers(current_pass_, 0, &vb_binding, 1);
    
    // Draw all vertices
    SDL_DrawGPUPrimitives(current_pass_, static_cast<uint32_t>(vertex_batch_.size()), 1, 0, 0);
    
    // Clear batch for next frame
    vertex_batch_.clear();
}

glm::vec4 UIRenderer::color_from_uint32(uint32_t color) const {
    float r = (color & 0xFF) / 255.0f;
    float g = ((color >> 8) & 0xFF) / 255.0f;
    float b = ((color >> 16) & 0xFF) / 255.0f;
    float a = ((color >> 24) & 0xFF) / 255.0f;
    return glm::vec4(r, g, b, a);
}

void UIRenderer::draw_quad(float x, float y, float w, float h, const glm::vec4& color) {
    // Check if we need to flush before adding more vertices
    if (vertex_batch_.size() + 6 > MAX_VERTICES) {
        flush_batch();
        
        // Re-bind UI pipeline after flush
        if (pipeline_registry_) {
            auto* ui_pipeline = pipeline_registry_->get_ui_pipeline();
            if (ui_pipeline && current_pass_) {
                ui_pipeline->bind(current_pass_);
            }
        }
        // Re-push projection matrix
        if (current_cmd_) {
            SDL_PushGPUVertexUniformData(current_cmd_, 0, &projection_, sizeof(glm::mat4));
        }
    }
    
    // Add 6 vertices for two triangles (quad)
    UIVertex v0 = {x, y, color.r, color.g, color.b, color.a};
    UIVertex v1 = {x + w, y, color.r, color.g, color.b, color.a};
    UIVertex v2 = {x + w, y + h, color.r, color.g, color.b, color.a};
    UIVertex v3 = {x, y + h, color.r, color.g, color.b, color.a};
    
    // Triangle 1
    vertex_batch_.push_back(v0);
    vertex_batch_.push_back(v1);
    vertex_batch_.push_back(v2);
    
    // Triangle 2
    vertex_batch_.push_back(v0);
    vertex_batch_.push_back(v2);
    vertex_batch_.push_back(v3);
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
    
    // Check if we have room for all circle vertices
    size_t vertices_needed = static_cast<size_t>(segments) * 3;
    if (vertex_batch_.size() + vertices_needed > MAX_VERTICES) {
        flush_batch();
        
        // Re-bind pipeline after flush
        if (pipeline_registry_ && current_pass_) {
            auto* ui_pipeline = pipeline_registry_->get_ui_pipeline();
            if (ui_pipeline) {
                ui_pipeline->bind(current_pass_);
            }
        }
        if (current_cmd_) {
            SDL_PushGPUVertexUniformData(current_cmd_, 0, &projection_, sizeof(glm::mat4));
        }
    }
    
    constexpr float PI = 3.14159265359f;
    for (int i = 0; i < segments; ++i) {
        float a1 = (i / static_cast<float>(segments)) * 2.0f * PI;
        float a2 = ((i + 1) / static_cast<float>(segments)) * 2.0f * PI;
        
        UIVertex center = {x, y, c.r, c.g, c.b, c.a};
        UIVertex p1 = {x + std::cos(a1) * radius, y + std::sin(a1) * radius, c.r, c.g, c.b, c.a};
        UIVertex p2 = {x + std::cos(a2) * radius, y + std::sin(a2) * radius, c.r, c.g, c.b, c.a};
        
        vertex_batch_.push_back(center);
        vertex_batch_.push_back(p1);
        vertex_batch_.push_back(p2);
    }
}

void UIRenderer::draw_circle_outline(float x, float y, float radius, uint32_t color, 
                                      float line_width, int segments) {
    // For now, just draw filled circle (outline requires more complex geometry)
    draw_circle(x, y, radius, color, segments);
}

void UIRenderer::draw_line(float x1, float y1, float x2, float y2, uint32_t color, float line_width) {
    glm::vec4 c = color_from_uint32(color);
    
    float dx = x2 - x1;
    float dy = y2 - y1;
    float len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.001f) return;
    
    // Calculate perpendicular offset for line width
    float nx = -dy / len * line_width / 2;
    float ny = dx / len * line_width / 2;
    
    // Check if we need to flush
    if (vertex_batch_.size() + 6 > MAX_VERTICES) {
        flush_batch();
        
        if (pipeline_registry_ && current_pass_) {
            auto* ui_pipeline = pipeline_registry_->get_ui_pipeline();
            if (ui_pipeline) {
                ui_pipeline->bind(current_pass_);
            }
        }
        if (current_cmd_) {
            SDL_PushGPUVertexUniformData(current_cmd_, 0, &projection_, sizeof(glm::mat4));
        }
    }
    
    // Create line quad vertices
    UIVertex v0 = {x1 + nx, y1 + ny, c.r, c.g, c.b, c.a};
    UIVertex v1 = {x1 - nx, y1 - ny, c.r, c.g, c.b, c.a};
    UIVertex v2 = {x2 - nx, y2 - ny, c.r, c.g, c.b, c.a};
    UIVertex v3 = {x2 + nx, y2 + ny, c.r, c.g, c.b, c.a};
    
    // Triangle 1
    vertex_batch_.push_back(v0);
    vertex_batch_.push_back(v1);
    vertex_batch_.push_back(v2);
    
    // Triangle 2
    vertex_batch_.push_back(v0);
    vertex_batch_.push_back(v2);
    vertex_batch_.push_back(v3);
}

void UIRenderer::draw_text(const std::string& text, float x, float y, uint32_t color, float scale) {
    if (text_renderer_ && text_renderer_->is_ready()) {
        // Flush UI batch before drawing text (text uses different pipeline)
        flush_batch();
        
        text_renderer_->set_projection(projection_);
        text_renderer_->draw_text(current_cmd_, current_pass_, text, x, y, color, scale);
        
        // Re-bind UI pipeline after text rendering
        if (pipeline_registry_ && current_pass_) {
            auto* ui_pipeline = pipeline_registry_->get_ui_pipeline();
            if (ui_pipeline) {
                ui_pipeline->bind(current_pass_);
            }
        }
        if (current_cmd_) {
            SDL_PushGPUVertexUniformData(current_cmd_, 0, &projection_, sizeof(glm::mat4));
        }
    }
}

void UIRenderer::draw_button(float x, float y, float w, float h, const std::string& label, 
                              uint32_t color, bool selected) {
    draw_filled_rect(x, y, w, h, color);
    uint32_t border_color = selected ? 0xFFFFFFFF : 0xFF888888;
    draw_rect_outline(x, y, w, h, border_color, selected ? 3.0f : 2.0f);
    
    if (text_renderer_ && text_renderer_->is_ready() && !label.empty()) {
        // Flush before text
        flush_batch();
        
        text_renderer_->set_projection(projection_);
        int text_w = text_renderer_->get_text_width(label, 1.0f);
        int text_h = text_renderer_->get_text_height(1.0f);
        float text_x = x + (w - text_w) / 2.0f;
        float text_y = y + (h - text_h) / 2.0f;
        text_renderer_->draw_text(current_cmd_, current_pass_, label, text_x, text_y, 0xFFFFFFFF, 1.0f);
        
        // Re-bind UI pipeline
        if (pipeline_registry_ && current_pass_) {
            auto* ui_pipeline = pipeline_registry_->get_ui_pipeline();
            if (ui_pipeline) {
                ui_pipeline->bind(current_pass_);
            }
        }
        if (current_cmd_) {
            SDL_PushGPUVertexUniformData(current_cmd_, 0, &projection_, sizeof(glm::mat4));
        }
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
