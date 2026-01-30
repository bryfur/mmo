#include "render_context.hpp"
#include <SDL3/SDL_log.h>

namespace mmo {

RenderContext::~RenderContext() {
    shutdown();
}

bool RenderContext::init(int width, int height, const std::string& title) {
    width_ = width;
    height_ = height;
    
    // Create window without OpenGL flag - SDL3 GPU handles its own context
    window_ = SDL_CreateWindow(title.c_str(), width, height, SDL_WINDOW_RESIZABLE);
    if (!window_) {
        SDL_Log("RenderContext::init: Failed to create window: %s", SDL_GetError());
        return false;
    }
    
    // Initialize the GPU device with the window
    if (!device_.init(window_)) {
        SDL_Log("RenderContext::init: Failed to initialize GPU device");
        SDL_DestroyWindow(window_);
        window_ = nullptr;
        return false;
    }
    
    SDL_Log("RenderContext::init: Initialized with %s backend", device_.driver_name().c_str());
    SDL_Log("RenderContext::init: Window size: %dx%d", width_, height_);
    
    return true;
}

void RenderContext::shutdown() {
    current_cmd_ = nullptr;
    device_.shutdown();
    
    if (window_) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }
    
    width_ = 0;
    height_ = 0;
}

void RenderContext::update_viewport() {
    if (window_) {
        SDL_GetWindowSize(window_, &width_, &height_);
        // Note: In SDL3 GPU, viewport is set per render pass, not globally
    }
}

void RenderContext::begin_frame() {
    if (current_cmd_ != nullptr) {
        SDL_Log("RenderContext::begin_frame: Warning - called while a frame is already in progress. "
                "Submitting previous frame to avoid command buffer leak.");
        end_frame(current_cmd_);
    }
    current_cmd_ = begin_frame_cmd();
}

SDL_GPUCommandBuffer* RenderContext::begin_frame_cmd() {
    update_viewport();
    return device_.begin_frame();
}

void RenderContext::end_frame() {
    end_frame(current_cmd_);
    current_cmd_ = nullptr;
}

void RenderContext::end_frame(SDL_GPUCommandBuffer* cmd) {
    device_.end_frame(cmd);
}

SDL_GPUTexture* RenderContext::acquire_swapchain_texture(SDL_GPUCommandBuffer* cmd,
                                                          uint32_t* out_width,
                                                          uint32_t* out_height) {
    return device_.acquire_swapchain_texture(cmd, out_width, out_height);
}

void RenderContext::set_vsync_mode(int mode) {
    if (vsync_mode_ == mode) return;

    // SDL3 GPU vsync is controlled via swapchain present mode
    // This requires recreating the swapchain with different parameters
    // For now, we just store the preference - actual implementation would need
    // SDL_SetGPUSwapchainParameters when that API is available
    vsync_mode_ = mode;
    SDL_Log("RenderContext::set_vsync_mode: VSync mode set to %d (requires swapchain recreation)", mode);
}

// =========================================================================
// Legacy State Management (no-ops for SDL3 GPU compatibility)
// In SDL3 GPU, these states are set per-pipeline, not globally.
// =========================================================================

void RenderContext::set_depth_test(bool /*enabled*/) {
    // No-op: Depth testing is configured in pipeline state
    // Use SDL_GPUDepthStencilState when creating pipelines
}

void RenderContext::set_depth_write(bool /*enabled*/) {
    // No-op: Depth writing is configured in pipeline state
    // Use SDL_GPUDepthStencilState.enable_depth_write when creating pipelines
}

void RenderContext::set_culling(bool /*enabled*/, int /*face*/) {
    // No-op: Culling is configured in pipeline state
    // Use SDL_GPURasterizerState when creating pipelines
}

void RenderContext::set_blending(bool /*enabled*/, int /*src*/, int /*dst*/) {
    // No-op: Blending is configured in pipeline state
    // Use SDL_GPUColorTargetBlendState when creating pipelines
}

} // namespace mmo
