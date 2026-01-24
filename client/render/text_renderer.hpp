#pragma once

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <string>
#include <unordered_map>

namespace mmo {

class Shader;

class TextRenderer {
public:
    TextRenderer();
    ~TextRenderer();
    
    bool init();
    void shutdown();
    
    // Set the shader and projection matrix before drawing
    void set_shader(Shader* shader);
    void set_projection(const glm::mat4& projection);
    void set_vao_vbo(GLuint vao, GLuint vbo);
    
    void draw_text(const std::string& text, float x, float y, uint32_t color = 0xFFFFFFFF, float scale = 1.0f);
    void draw_text_centered(const std::string& text, float x, float y, uint32_t color = 0xFFFFFFFF, float scale = 1.0f);
    
    int get_text_width(const std::string& text, float scale = 1.0f);
    int get_text_height(float scale = 1.0f);
    
    bool is_ready() const { return initialized_ && font_ != nullptr; }
    
private:
    TTF_Font* font_ = nullptr;
    int font_size_ = 18;
    bool initialized_ = false;
    
    Shader* shader_ = nullptr;
    glm::mat4 projection_;
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
};

} // namespace mmo
