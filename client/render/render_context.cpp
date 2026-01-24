#include "render_context.hpp"
#include <iostream>

namespace mmo {

RenderContext::~RenderContext() {
    shutdown();
}

bool RenderContext::init(int width, int height, const std::string& title) {
    width_ = width;
    height_ = height;
    
    // Initialize SDL
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << "Failed to initialize SDL: " << SDL_GetError() << std::endl;
        return false;
    }
    
    // Create window (no OpenGL flags - bgfx handles rendering backend)
    window_ = SDL_CreateWindow(
        title.c_str(),
        width,
        height,
        SDL_WINDOW_RESIZABLE
    );
    
    if (!window_) {
        std::cerr << "Failed to create SDL window: " << SDL_GetError() << std::endl;
        return false;
    }
    
    // Get native window handle for bgfx using SDL3 properties
    bgfx::PlatformData pd{};
    SDL_PropertiesID props = SDL_GetWindowProperties(window_);
    
    if (props == 0) {
        std::cerr << "Failed to get SDL window properties" << std::endl;
        return false;
    }

#if defined(__linux__)
    // Check which display server is in use and configure accordingly
    // Try Wayland first (preferred on modern systems)
    void* wl_display = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, nullptr);
    void* wl_surface = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr);
    
    if (wl_display && wl_surface) {
        pd.ndt = wl_display;
        pd.nwh = wl_surface;
        pd.type = bgfx::NativeWindowHandleType::Wayland;
        std::cout << "Using Wayland display server" << std::endl;
    } else {
        // Fall back to X11
        void* x11_display = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
        Sint64 x11_window = SDL_GetNumberProperty(props, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0);
        
        if (x11_display && x11_window != 0) {
            pd.ndt = x11_display;
            pd.nwh = reinterpret_cast<void*>(static_cast<uintptr_t>(x11_window));
            pd.type = bgfx::NativeWindowHandleType::Default;
            std::cout << "Using X11 display server" << std::endl;
        } else {
            std::cerr << "Failed to get native window handle (neither Wayland nor X11 available)" << std::endl;
            std::cerr << "Wayland display: " << wl_display << ", Wayland surface: " << wl_surface << std::endl;
            std::cerr << "X11 display: " << x11_display << ", X11 window: " << x11_window << std::endl;
            return false;
        }
    }
#elif defined(_WIN32)
    pd.nwh = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
#elif defined(__APPLE__)
    pd.nwh = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
#endif
    
    if (!pd.nwh) {
        std::cerr << "Failed to get native window handle" << std::endl;
        return false;
    }
    
    // Initialize bgfx
    bgfx::Init init;
    init.type = bgfx::RendererType::Count;  // Auto-select best renderer (Vulkan on Linux)
    init.vendorId = BGFX_PCI_ID_NONE;
    init.resolution.width = width_;
    init.resolution.height = height_;
    init.resolution.reset = BGFX_RESET_VSYNC;
    init.platformData = pd;
    
    if (!bgfx::init(init)) {
        std::cerr << "Failed to initialize bgfx" << std::endl;
        return false;
    }
    
    // Set up default view
    bgfx::setViewClear(ViewId::Main, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, clear_color_, 1.0f, 0);
    bgfx::setViewRect(ViewId::Main, 0, 0, width_, height_);
    
    // Set up UI view (no depth clear, rendered on top)
    bgfx::setViewClear(ViewId::UI, BGFX_CLEAR_NONE);
    bgfx::setViewRect(ViewId::UI, 0, 0, width_, height_);
    bgfx::setViewMode(ViewId::UI, bgfx::ViewMode::Sequential);
    
    // Disable verbose debug output (set to BGFX_DEBUG_TEXT to re-enable debug overlay)
    bgfx::setDebug(BGFX_DEBUG_NONE);
    
    initialized_ = true;
    
    std::cout << "bgfx initialized with renderer: " << bgfx::getRendererName(bgfx::getRendererType()) << std::endl;
    
    return true;
}

void RenderContext::shutdown() {
    if (initialized_) {
        bgfx::shutdown();
        initialized_ = false;
    }
    
    if (window_) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }
    
    SDL_Quit();
}

void RenderContext::update_viewport() {
    if (!window_) return;
    
    int w, h;
    SDL_GetWindowSize(window_, &w, &h);
    
    if (w != width_ || h != height_) {
        width_ = w;
        height_ = h;
        bgfx::reset(width_, height_, BGFX_RESET_VSYNC);
        bgfx::setViewRect(ViewId::Main, 0, 0, width_, height_);
        bgfx::setViewRect(ViewId::UI, 0, 0, width_, height_);
    }
}

void RenderContext::begin_frame() {
    // Update viewport in case window was resized
    update_viewport();
    
    // Touch main view to ensure it's rendered
    bgfx::touch(ViewId::Main);
}

void RenderContext::end_frame() {
    // Frame submission - this actually renders everything
    bgfx::frame();
}

void RenderContext::set_view_clear(bgfx::ViewId id, uint32_t color, float depth) {
    bgfx::setViewClear(id, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, color, depth, 0);
}

void RenderContext::set_view_rect(bgfx::ViewId id, int x, int y, int w, int h) {
    bgfx::setViewRect(id, x, y, w, h);
}

void RenderContext::set_view_transform(bgfx::ViewId id, const glm::mat4& view, const glm::mat4& proj) {
    bgfx::setViewTransform(id, &view[0][0], &proj[0][0]);
}

void RenderContext::touch(bgfx::ViewId id) {
    bgfx::touch(id);
}

} // namespace mmo
