#pragma once

#include <SDL3/SDL.h>
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <string>
#include <memory>

namespace mmo {

/**
 * RenderContext manages the SDL window, OpenGL context, and core rendering state.
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
     * Initialize SDL window and OpenGL context.
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
     * Begin a new frame - clear buffers, set default state.
     */
    void begin_frame();
    
    /**
     * End frame - swap buffers.
     */
    void end_frame();
    
    // Accessors
    SDL_Window* window() const { return window_; }
    SDL_GLContext gl_context() const { return gl_context_; }
    int width() const { return width_; }
    int height() const { return height_; }
    float aspect_ratio() const { return static_cast<float>(width_) / height_; }
    
    /**
     * Enable/disable common GL states.
     */
    void set_depth_test(bool enabled);
    void set_depth_write(bool enabled);
    void set_culling(bool enabled, GLenum face = GL_BACK);
    void set_blending(bool enabled, GLenum src = GL_SRC_ALPHA, GLenum dst = GL_ONE_MINUS_SRC_ALPHA);
    
private:
    SDL_Window* window_ = nullptr;
    SDL_GLContext gl_context_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    
    // Clear color
    glm::vec4 clear_color_ = glm::vec4(0.05f, 0.07f, 0.1f, 1.0f);
};

} // namespace mmo
