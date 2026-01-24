#pragma once

#include <SDL3/SDL.h>
#include <bgfx/bgfx.h>
#include <bgfx/platform.h>
#include <glm/glm.hpp>
#include <string>
#include <memory>

namespace mmo {

// View IDs for bgfx render passes
namespace ViewId {
    constexpr bgfx::ViewId Shadow = 0;
    constexpr bgfx::ViewId SSAO_GBuffer = 1;
    constexpr bgfx::ViewId SSAO_Calc = 2;
    constexpr bgfx::ViewId SSAO_Blur = 3;
    constexpr bgfx::ViewId Main = 4;
    constexpr bgfx::ViewId UI = 5;
}

/**
 * RenderContext manages the SDL window and bgfx context.
 * This is the foundation that other renderers build upon.
 */
class RenderContext {
public:
    RenderContext() = default;
    ~RenderContext();
    
    // Non-copyable
    RenderContext(const RenderContext&) = delete;
    RenderContext& operator=(const RenderContext&) = delete;
    
    /**
     * Initialize SDL window and bgfx context.
     * @return true on success
     */
    bool init(int width, int height, const std::string& title);
    
    /**
     * Clean up all resources.
     */
    void shutdown();
    
    /**
     * Update window size (call after resize events).
     */
    void update_viewport();
    
    /**
     * Begin a new frame.
     */
    void begin_frame();
    
    /**
     * End frame - submit to bgfx.
     */
    void end_frame();
    
    // Accessors
    SDL_Window* window() const { return window_; }
    int width() const { return width_; }
    int height() const { return height_; }
    float aspect_ratio() const { return static_cast<float>(width_) / height_; }
    
    // bgfx state helpers
    void set_view_clear(bgfx::ViewId id, uint32_t color, float depth = 1.0f);
    void set_view_rect(bgfx::ViewId id, int x, int y, int w, int h);
    void set_view_transform(bgfx::ViewId id, const glm::mat4& view, const glm::mat4& proj);
    
    // Touch a view to ensure it gets rendered even if empty
    void touch(bgfx::ViewId id);
    
private:
    SDL_Window* window_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    bool initialized_ = false;
    
    // Clear color
    uint32_t clear_color_ = 0x0d121aff;  // Dark blue-gray
};

} // namespace mmo
