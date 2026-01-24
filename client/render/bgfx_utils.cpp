#include "bgfx_utils.hpp"
#include <SDL3_image/SDL_image.h>
#include <fstream>
#include <iostream>

namespace mmo {
namespace bgfx_utils {

bgfx::ShaderHandle load_shader(const char* name) {
    std::string path = std::string("shaders/") + name + ".bin";
    
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::cerr << "Failed to open shader: " << path << std::endl;
        return BGFX_INVALID_HANDLE;
    }
    
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    
    const bgfx::Memory* mem = bgfx::alloc(static_cast<uint32_t>(size + 1));
    if (!file.read(reinterpret_cast<char*>(mem->data), size)) {
        std::cerr << "Failed to read shader: " << path << std::endl;
        return BGFX_INVALID_HANDLE;
    }
    mem->data[size] = '\0';
    
    return bgfx::createShader(mem);
}

bgfx::ProgramHandle load_program(const char* vs_name, const char* fs_name) {
    bgfx::ShaderHandle vs = load_shader(vs_name);
    bgfx::ShaderHandle fs = load_shader(fs_name);
    
    if (!bgfx::isValid(vs) || !bgfx::isValid(fs)) {
        if (bgfx::isValid(vs)) bgfx::destroy(vs);
        if (bgfx::isValid(fs)) bgfx::destroy(fs);
        return BGFX_INVALID_HANDLE;
    }
    
    return bgfx::createProgram(vs, fs, true);  // destroyShaders = true
}

bgfx::TextureHandle load_texture(const char* path, uint64_t flags) {
    SDL_Surface* surface = IMG_Load(path);
    if (!surface) {
        std::cerr << "Failed to load texture: " << path << " - " << SDL_GetError() << std::endl;
        return BGFX_INVALID_HANDLE;
    }
    
    // Convert to RGBA format if needed
    SDL_Surface* rgba_surface = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(surface);
    
    if (!rgba_surface) {
        std::cerr << "Failed to convert texture to RGBA: " << path << std::endl;
        return BGFX_INVALID_HANDLE;
    }
    
    int tex_width = rgba_surface->w;
    int tex_height = rgba_surface->h;
    
    const bgfx::Memory* mem = bgfx::copy(
        rgba_surface->pixels,
        static_cast<uint32_t>(tex_width * tex_height * 4)
    );
    
    bgfx::TextureHandle texture = bgfx::createTexture2D(
        static_cast<uint16_t>(tex_width),
        static_cast<uint16_t>(tex_height),
        true, 1,  // hasMips, numLayers
        bgfx::TextureFormat::RGBA8,
        flags,
        mem
    );
    
    SDL_DestroySurface(rgba_surface);
    
    if (bgfx::isValid(texture)) {
        std::cout << "Loaded texture: " << path << " (" << tex_width << "x" << tex_height << ")" << std::endl;
    }
    
    return texture;
}

} // namespace bgfx_utils
} // namespace mmo
