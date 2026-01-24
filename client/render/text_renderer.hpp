#pragma once

#include "render_context.hpp"
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <bgfx/bgfx.h>
#include <glm/glm.hpp>
#include <string>

namespace mmo {

/**
 * TextRenderer handles text rendering using SDL_ttf and bgfx.
 * Creates textures on-the-fly from rendered text.
 */
class TextRenderer {
public:
    TextRenderer();
    ~TextRenderer();
    
    bool init(int screen_width, int screen_height);
    void shutdown();
    
    void set_screen_size(int width, int height);
    
    void draw_text(const std::string& text, float x, float y, uint32_t color = 0xFFFFFFFF, float scale = 1.0f);
    void draw_text_centered(const std::string& text, float x, float y, uint32_t color = 0xFFFFFFFF, float scale = 1.0f);
    
    int get_text_width(const std::string& text, float scale = 1.0f);
    int get_text_height(float scale = 1.0f);
    
    bool is_ready() const { return initialized_ && font_ != nullptr; }
    
private:
    TTF_Font* font_ = nullptr;
    int font_size_ = 18;
    bool initialized_ = false;
    
    int screen_width_ = 0;
    int screen_height_ = 0;
    glm::mat4 projection_;
    
    bgfx::ProgramHandle text_program_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_textColor_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_textTexture_ = BGFX_INVALID_HANDLE;
    
    bgfx::VertexLayout text_layout_;
};

} // namespace mmo
