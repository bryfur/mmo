#pragma once

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>
#include <glm/glm.hpp>
#include <string>
#include "engine/gpu/gpu_device.hpp"

namespace mmo::engine::render {

namespace gpu = mmo::engine::gpu;

/**
 * RenderContext manages the SDL window and GPU device for SDL3 GPU API rendering.
 * This is the foundation that other renderers build upon.
 * 
 * Migration from OpenGL: This class now wraps GPUDevice instead of OpenGL context.
 * State management (depth test, blending, etc.) is now handled through pipelines,
 * not through global state functions.
 */
class RenderContext {
public:
    RenderContext() = default;
    ~RenderContext();
    
    // Non-copyable, non-movable (owns GPU resources)
    RenderContext(const RenderContext&) = delete;
    RenderContext& operator=(const RenderContext&) = delete;
    RenderContext(RenderContext&&) = delete;
    RenderContext& operator=(RenderContext&&) = delete;
    
    /**
     * Initialize SDL window and GPU device.
     * @return true on success
     */
    bool init(int width, int height, const std::string& title);
    
    /**
     * Clean up all resources.
     */
    void shutdown();
    
    /**
     * Update cached window dimensions (call after resize events).
     */
    void update_viewport();
    
    /**
     * Begin a new frame - acquire command buffer.
     * Legacy void version stores command buffer internally for compatibility.
     */
    void begin_frame();
    
    /**
     * Begin a new frame - acquire command buffer.
     * @return Command buffer for the frame, or nullptr on failure
     */
    SDL_GPUCommandBuffer* begin_frame_cmd();
    
    /**
     * End frame - submit command buffer and present.
     * Legacy void version uses internally stored command buffer.
     */
    void end_frame();
    
    /**
     * End frame - submit command buffer and present.
     * @param cmd The command buffer to submit
     */
    void end_frame(SDL_GPUCommandBuffer* cmd);
    
    /**
     * Get the current frame's command buffer (only valid between begin_frame and end_frame)
     */
    SDL_GPUCommandBuffer* current_command_buffer() const { return current_cmd_; }
    
    // Accessors
    SDL_Window* window() const { return window_; }
    gpu::GPUDevice& device() { return device_; }
    const gpu::GPUDevice& device() const { return device_; }
    int width() const { return width_; }
    int height() const { return height_; }
    float aspect_ratio() const { return height_ > 0 ? static_cast<float>(width_) / height_ : 1.0f; }
    
    /**
     * Get the swapchain texture format for pipeline creation
     */
    SDL_GPUTextureFormat swapchain_format() const { return device_.swapchain_format(); }
    
    /**
     * Acquire swapchain texture for rendering
     */
    SDL_GPUTexture* acquire_swapchain_texture(SDL_GPUCommandBuffer* cmd,
                                               uint32_t* out_width = nullptr,
                                               uint32_t* out_height = nullptr);
    
    /**
     * Set VSync mode: 0=off, 1=vsync, 2=adaptive vsync
     * Note: In SDL3 GPU, this is controlled via swapchain present mode
     */
    void set_vsync_mode(int mode);
    
    /**
     * Get clear color for render passes
     */
    const glm::vec4& clear_color() const { return clear_color_; }
    
    /**
     * Set clear color for render passes
     */
    void set_clear_color(const glm::vec4& color) { clear_color_ = color; }
    
    // =========================================================================
    // Legacy State Management (deprecated - use pipelines instead)
    // These are no-ops in SDL3 GPU API since state is bound to pipelines.
    // Kept for compatibility during migration.
    // =========================================================================
    
    /** @deprecated Use pipeline depth state instead */
    void set_depth_test(bool enabled);
    /** @deprecated Use pipeline depth state instead */
    void set_depth_write(bool enabled);
    /** @deprecated Use pipeline rasterizer state instead */
    void set_culling(bool enabled, int face = 0);
    /** @deprecated Use pipeline blend state instead */
    void set_blending(bool enabled, int src = 0, int dst = 0);
    
private:
    SDL_Window* window_ = nullptr;
    gpu::GPUDevice device_;
    int width_ = 0;
    int height_ = 0;
    int vsync_mode_ = 1;  // Track desired vsync mode
    
    // Current frame's command buffer (valid between begin_frame and end_frame)
    SDL_GPUCommandBuffer* current_cmd_ = nullptr;
    
    // Clear color for render passes
    glm::vec4 clear_color_ = glm::vec4(0.05f, 0.07f, 0.1f, 1.0f);
};

} // namespace mmo::engine::render
