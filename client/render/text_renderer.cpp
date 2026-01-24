#include "text_renderer.hpp"
#include "bgfx_utils.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>

namespace mmo {

TextRenderer::TextRenderer() {}

TextRenderer::~TextRenderer() {
    shutdown();
}

bool TextRenderer::init(int screen_width, int screen_height) {
    if (initialized_) return true;
    
    screen_width_ = screen_width;
    screen_height_ = screen_height;
    
    if (!TTF_Init()) {
        std::cerr << "Failed to initialize SDL_ttf: " << SDL_GetError() << std::endl;
        return false;
    }
    
    // Try to load a default font
    const char* font_paths[] = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/ubuntu/Ubuntu-R.ttf",
        "/usr/share/fonts/truetype/freefont/FreeSans.ttf",
        "/System/Library/Fonts/Helvetica.ttc",
        "C:\\Windows\\Fonts\\arial.ttf"
    };
    
    for (const char* path : font_paths) {
        font_ = TTF_OpenFont(path, font_size_);
        if (font_) {
            std::cout << "Loaded font: " << path << std::endl;
            break;
        }
    }
    
    if (!font_) {
        std::cerr << "Warning: Could not load any font, text rendering disabled" << std::endl;
        // Continue without font - not fatal
    }
    
    // Set up vertex layout for text (position 2D + texcoord 2D)
    text_layout_
        .begin()
        .add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .end();
    
    // Load text shader
    text_program_ = bgfx_utils::load_program("text_vs", "text_fs");
    if (!bgfx::isValid(text_program_)) {
        std::cerr << "Failed to load text shader program" << std::endl;
        return false;
    }
    
    // Create uniforms
    u_textColor_ = bgfx::createUniform("u_textColor", bgfx::UniformType::Vec4);
    s_textTexture_ = bgfx::createUniform("s_textTexture", bgfx::UniformType::Sampler);
    
    // Set up projection
    set_screen_size(screen_width, screen_height);
    
    initialized_ = true;
    return true;
}

void TextRenderer::shutdown() {
    if (font_) {
        TTF_CloseFont(font_);
        font_ = nullptr;
    }
    
    if (bgfx::isValid(text_program_)) {
        bgfx::destroy(text_program_);
        text_program_ = BGFX_INVALID_HANDLE;
    }
    
    if (bgfx::isValid(u_textColor_)) {
        bgfx::destroy(u_textColor_);
        u_textColor_ = BGFX_INVALID_HANDLE;
    }
    
    if (bgfx::isValid(s_textTexture_)) {
        bgfx::destroy(s_textTexture_);
        s_textTexture_ = BGFX_INVALID_HANDLE;
    }
    
    if (initialized_) {
        TTF_Quit();
        initialized_ = false;
    }
}

void TextRenderer::set_screen_size(int width, int height) {
    screen_width_ = width;
    screen_height_ = height;
    projection_ = glm::ortho(0.0f, static_cast<float>(width), 
                              static_cast<float>(height), 0.0f, -1.0f, 1.0f);
}

void TextRenderer::draw_text(const std::string& text, float x, float y, uint32_t color, float scale) {
    if (!font_ || text.empty() || !bgfx::isValid(text_program_)) {
        return;
    }
    
    // Extract color components (ABGR format)
    uint8_t r = color & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = (color >> 16) & 0xFF;
    uint8_t a = (color >> 24) & 0xFF;
    
    SDL_Color sdl_color = {255, 255, 255, 255};  // Render white, tint with shader
    
    SDL_Surface* surface = TTF_RenderText_Blended(font_, text.c_str(), 0, sdl_color);
    if (!surface) {
        std::cerr << "Failed to render text surface: " << SDL_GetError() << std::endl;
        return;
    }
    
    // Convert surface to RGBA format if needed
    SDL_Surface* converted = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(surface);
    if (!converted) {
        std::cerr << "Failed to convert text surface: " << SDL_GetError() << std::endl;
        return;
    }
    
    // Create bgfx texture from surface
    const bgfx::Memory* mem = bgfx::copy(converted->pixels, converted->w * converted->h * 4);
    bgfx::TextureHandle texture = bgfx::createTexture2D(
        uint16_t(converted->w),
        uint16_t(converted->h),
        false, 1,
        bgfx::TextureFormat::RGBA8,
        BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT,
        mem
    );
    
    // Calculate dimensions
    float w = converted->w * scale;
    float h = converted->h * scale;
    
    SDL_DestroySurface(converted);
    
    if (!bgfx::isValid(texture)) {
        return;
    }
    
    // Build quad vertices (pos.x, pos.y, tex.x, tex.y)
    if (bgfx::getAvailTransientVertexBuffer(6, text_layout_) < 6) {
        bgfx::destroy(texture);
        return;
    }
    
    bgfx::TransientVertexBuffer tvb;
    bgfx::allocTransientVertexBuffer(&tvb, 6, text_layout_);
    
    struct TextVertex {
        float x, y;
        float u, v;
    };
    
    TextVertex* vertices = (TextVertex*)tvb.data;
    vertices[0] = {x,     y,     0.0f, 0.0f};
    vertices[1] = {x + w, y,     1.0f, 0.0f};
    vertices[2] = {x + w, y + h, 1.0f, 1.0f};
    vertices[3] = {x,     y,     0.0f, 0.0f};
    vertices[4] = {x + w, y + h, 1.0f, 1.0f};
    vertices[5] = {x,     y + h, 0.0f, 1.0f};
    
    // Set text color uniform
    float textColor[4] = {r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f};
    bgfx::setUniform(u_textColor_, textColor);
    
    // Set texture
    bgfx::setTexture(0, s_textTexture_, texture);
    
    bgfx::setVertexBuffer(0, &tvb);
    
    // Set state: alpha blending, no depth test
    uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
                   | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA);
    bgfx::setState(state);
    
    bgfx::submit(ViewId::UI, text_program_);
    
    // Destroy the texture after submission (bgfx queues the destruction)
    bgfx::destroy(texture);
}

void TextRenderer::draw_text_centered(const std::string& text, float x, float y, uint32_t color, float scale) {
    int w = get_text_width(text, scale);
    int h = get_text_height(scale);
    draw_text(text, x - w / 2.0f, y - h / 2.0f, color, scale);
}

int TextRenderer::get_text_width(const std::string& text, float scale) {
    if (!font_ || text.empty()) return 0;
    
    int w = 0, h = 0;
    if (TTF_GetStringSize(font_, text.c_str(), text.length(), &w, &h)) {
        return static_cast<int>(w * scale);
    }
    return 0;
}

int TextRenderer::get_text_height(float scale) {
    if (!font_) return 0;
    return static_cast<int>(TTF_GetFontHeight(font_) * scale);
}

} // namespace mmo
