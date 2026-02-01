#include "render_context.hpp"
#include "SDL3/SDL_error.h"
#include "SDL3/SDL_gpu.h"
#include "SDL3/SDL_video.h"
#include <SDL3/SDL_log.h>
#include <cstdint>
#include <string>

namespace mmo::engine::render {

namespace gpu = mmo::engine::gpu;

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

int RenderContext::max_vsync_mode() const {
    if (device_.supports_present_mode(SDL_GPU_PRESENTMODE_MAILBOX)) return 2;
    if (device_.supports_present_mode(SDL_GPU_PRESENTMODE_VSYNC)) return 1;
    return 0;
}

void RenderContext::set_vsync_mode(int mode) {
    if (vsync_mode_ == mode) return;
    vsync_mode_ = mode;

    // Map our mode to SDL present modes:
    // 0 = off (immediate), 1 = vsync, 2 = triple buffer (mailbox)
    SDL_GPUPresentMode present_mode = SDL_GPU_PRESENTMODE_IMMEDIATE;
    switch (mode) {
        case 1:  present_mode = SDL_GPU_PRESENTMODE_VSYNC; break;
        case 2:  present_mode = SDL_GPU_PRESENTMODE_MAILBOX; break;
        default: present_mode = SDL_GPU_PRESENTMODE_IMMEDIATE; break;
    }
    device_.set_swapchain_parameters(present_mode);
}

void RenderContext::query_display_modes() {
    available_resolutions_.clear();
    if (!window_) return;

    SDL_DisplayID display = SDL_GetDisplayForWindow(window_);
    int count = 0;
    const SDL_DisplayMode* const* modes = SDL_GetFullscreenDisplayModes(display, &count);
    if (!modes) return;

    for (int i = 0; i < count; i++) {
        // Deduplicate by w×h — SDL may list the same resolution at different refresh rates
        bool duplicate = false;
        for (const auto& existing : available_resolutions_) {
            if (existing.w == modes[i]->w && existing.h == modes[i]->h &&
                existing.pixel_density == modes[i]->pixel_density) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            available_resolutions_.push_back({modes[i]->w, modes[i]->h,
                                              modes[i]->refresh_rate, modes[i]->pixel_density});
        }
    }
}

void RenderContext::set_window_mode(int window_mode, int resolution_index) {
    if (!window_) return;

    if (window_mode == 0) {
        // Windowed
        SDL_SetWindowFullscreenMode(window_, nullptr);
        SDL_SetWindowFullscreen(window_, false);
    } else if (window_mode == 1) {
        // Borderless fullscreen
        SDL_SetWindowFullscreenMode(window_, nullptr);
        SDL_SetWindowFullscreen(window_, true);
    } else {
        // Exclusive fullscreen — use selected display mode
        if (available_resolutions_.empty()) query_display_modes();

        SDL_DisplayID display = SDL_GetDisplayForWindow(window_);
        int count = 0;
        const SDL_DisplayMode* const* modes = SDL_GetFullscreenDisplayModes(display, &count);
        const SDL_DisplayMode* selected = nullptr;

        if (modes && resolution_index >= 0 &&
            resolution_index < static_cast<int>(available_resolutions_.size())) {
            const auto& res = available_resolutions_[resolution_index];
            for (int i = 0; i < count; i++) {
                if (modes[i]->w == res.w && modes[i]->h == res.h &&
                    modes[i]->pixel_density == res.pixel_density) {
                    selected = modes[i];
                    break;
                }
            }
        }

        if (selected) {
            SDL_SetWindowFullscreenMode(window_, selected);
        }
        SDL_SetWindowFullscreen(window_, true);
    }
    update_viewport();
}

} // namespace mmo::engine::render
