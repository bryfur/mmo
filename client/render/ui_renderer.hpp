#pragma once

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>

// Forward declarations
namespace mmo::gpu {
    class GPUDevice;
    class GPUBuffer;
    class GPUPipeline;
    class PipelineRegistry;
}

namespace mmo {

class TextRenderer;

/**
 * UIRenderer handles all 2D UI rendering using SDL3 GPU API:
 * - Rectangles (filled/outline)
 * - Circles
 * - Lines
 * - Text
 * - Buttons
 * - Health bars
 * 
 * SDL3 GPU Migration: This renderer now uses GPUBuffer and GPUPipeline
 * instead of OpenGL VAO/VBO and shader programs. Pipeline state (blending,
 * depth test) is handled by the UI pipeline configuration.
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
     * @param device GPU device for creating resources
     * @param pipeline_registry Registry for accessing UI pipeline
     * @param width Screen width
     * @param height Screen height
     */
    bool init(gpu::GPUDevice& device, gpu::PipelineRegistry& pipeline_registry, 
              int width, int height);
    
    /**
     * Clean up resources.
     */
    void shutdown();
    
    /**
     * Update screen dimensions (call on resize).
     */
    void set_screen_size(int width, int height);
    
    /**
     * Begin UI rendering pass.
     * @param cmd Command buffer for the frame
     * @param render_pass Active render pass to draw into
     */
    void begin(SDL_GPUCommandBuffer* cmd, SDL_GPURenderPass* render_pass);
    
    /**
     * End UI rendering pass.
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
    void flush_batch();
    glm::vec4 color_from_uint32(uint32_t color) const;
    
    int width_ = 0;
    int height_ = 0;
    
    gpu::GPUDevice* device_ = nullptr;
    gpu::PipelineRegistry* pipeline_registry_ = nullptr;
    
    std::unique_ptr<TextRenderer> text_renderer_;
    
    glm::mat4 projection_;
    
    // SDL3 GPU resources
    std::unique_ptr<gpu::GPUBuffer> vertex_buffer_;
    
    // Current frame rendering state
    SDL_GPUCommandBuffer* current_cmd_ = nullptr;
    SDL_GPURenderPass* current_pass_ = nullptr;
    
    // Vertex batching for efficient rendering
    static constexpr size_t MAX_VERTICES = 4096;
    struct UIVertex {
        float x, y;           // Position
        float r, g, b, a;     // Color
    };
    std::vector<UIVertex> vertex_batch_;
};

} // namespace mmo
