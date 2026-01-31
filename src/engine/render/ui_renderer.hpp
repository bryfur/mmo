#pragma once

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>

// Forward declarations
namespace mmo::engine::gpu {
    class GPUDevice;
    class GPUBuffer;
    class GPUPipeline;
    class PipelineRegistry;
}

namespace mmo::engine::render {

namespace gpu = mmo::engine::gpu;

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
     * Begin UI recording phase.
     * @param cmd Command buffer for the frame
     * Note: This only starts recording, not rendering. Call execute() to render.
     */
    void begin(SDL_GPUCommandBuffer* cmd);

    /**
     * End UI recording phase.
     * After this, call execute() to upload data and render.
     */
    void end();

    /**
     * Upload all recorded UI data (copy pass) and render (render pass).
     * Must be called AFTER end() and before frame submission.
     * @param cmd Command buffer for the frame
     * @param swapchain Swapchain texture to render to
     * @param clear_background If true, clear to black. If false, preserve existing content.
     */
    void execute(SDL_GPUCommandBuffer* cmd, SDL_GPUTexture* swapchain, bool clear_background = false);
    
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
    SDL_GPUTexture* dummy_texture_ = nullptr;  // 1x1 white texture for when no texture needed
    SDL_GPUSampler* dummy_sampler_ = nullptr;

    // Current frame rendering state
    SDL_GPUCommandBuffer* current_cmd_ = nullptr;
    SDL_GPURenderPass* current_pass_ = nullptr;
    
    // Vertex batching for efficient rendering
    static constexpr size_t MAX_VERTICES = 4096;
    // Must match Vertex2D layout: position (2), texcoord (2), color (4)
    struct UIVertex {
        float x, y;           // Position
        float u, v;           // Texcoord (unused for solid colors, set to 0)
        float r, g, b, a;     // Color
    };
    std::vector<UIVertex> vertex_batch_;

    // Queued text draws (executed during execute() phase)
    struct QueuedTextDraw {
        std::string text;
        float x, y;
        uint32_t color;
        float scale;
    };
    std::vector<QueuedTextDraw> queued_text_draws_;
};

} // namespace mmo::engine::render
