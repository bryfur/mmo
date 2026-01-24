#include "text_renderer.hpp"
#include "../shader.hpp"
#include <iostream>
#include <glm/gtc/type_ptr.hpp>

namespace mmo {

TextRenderer::TextRenderer() {}

TextRenderer::~TextRenderer() {
    shutdown();
}

bool TextRenderer::init() {
    if (initialized_) return true;
    
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
    
    initialized_ = true;
    return true;
}

void TextRenderer::shutdown() {
    if (font_) {
        TTF_CloseFont(font_);
        font_ = nullptr;
    }
    if (initialized_) {
        TTF_Quit();
        initialized_ = false;
    }
}

void TextRenderer::set_shader(Shader* shader) {
    shader_ = shader;
}

void TextRenderer::set_projection(const glm::mat4& projection) {
    projection_ = projection;
}

void TextRenderer::set_vao_vbo(GLuint vao, GLuint vbo) {
    vao_ = vao;
    vbo_ = vbo;
}

void TextRenderer::draw_text(const std::string& text, float x, float y, uint32_t color, float scale) {
    if (!font_ || text.empty() || !shader_ || vao_ == 0) {
        if (!font_) std::cerr << "TextRenderer: No font loaded" << std::endl;
        if (!shader_) std::cerr << "TextRenderer: No shader set" << std::endl;
        if (vao_ == 0) std::cerr << "TextRenderer: No VAO set" << std::endl;
        return;
    }
    
    // Extract color components (ABGR format - same as rest of renderer)
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
    
    // Create OpenGL texture from surface
    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, converted->w, converted->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, converted->pixels);
    
    // Calculate dimensions
    float w = converted->w * scale;
    float h = converted->h * scale;
    
    // Build quad vertices (pos.x, pos.y, tex.x, tex.y)
    float vertices[] = {
        x,     y,     0.0f, 0.0f,
        x + w, y,     1.0f, 0.0f,
        x + w, y + h, 1.0f, 1.0f,
        
        x,     y,     0.0f, 0.0f,
        x + w, y + h, 1.0f, 1.0f,
        x,     y + h, 0.0f, 1.0f
    };
    
    // Enable blending for text
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    // Use shader
    shader_->use();
    shader_->set_mat4("projection", projection_);
    shader_->set_vec4("textColor", glm::vec4(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f));
    shader_->set_int("textTexture", 0);
    
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    
    // Upload and draw
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
    
    glDrawArrays(GL_TRIANGLES, 0, 6);
    
    glBindVertexArray(0);
    glDeleteTextures(1, &texture);
    SDL_DestroySurface(converted);
}

void TextRenderer::draw_text_centered(const std::string& text, float x, float y, uint32_t color, float scale) {
    int width = get_text_width(text, scale);
    draw_text(text, x - width / 2.0f, y, color, scale);
}

int TextRenderer::get_text_width(const std::string& text, float scale) {
    if (!font_ || text.empty()) return 0;
    
    int w = 0, h = 0;
    TTF_GetStringSize(font_, text.c_str(), 0, &w, &h);
    return static_cast<int>(w * scale);
}

int TextRenderer::get_text_height(float scale) {
    if (!font_) return 0;
    return static_cast<int>(TTF_GetFontHeight(font_) * scale);
}

} // namespace mmo
