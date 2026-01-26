#include "text_renderer.hpp"
#include "../gpu/gpu_device.hpp"
#include "../gpu/gpu_buffer.hpp"
#include "../gpu/gpu_pipeline.hpp"
#include "../gpu/pipeline_registry.hpp"
#include <iostream>
#include <glm/gtc/type_ptr.hpp>
#include <cstring>

namespace mmo {

TextRenderer::TextRenderer() {}

TextRenderer::~TextRenderer() {
    shutdown();
}

bool TextRenderer::init(gpu::GPUDevice& device, gpu::PipelineRegistry& pipeline_registry) {
    if (initialized_) return true;
    
    device_ = &device;
    pipeline_registry_ = &pipeline_registry;
    
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
    
    // Create dynamic vertex buffer for text quads
    // Each vertex: x, y, u, v (4 floats), 6 vertices per quad
    vertex_buffer_ = gpu::GPUBuffer::create_dynamic(
        device,
        gpu::GPUBuffer::Type::Vertex,
        6 * 4 * sizeof(float)
    );
    
    if (!vertex_buffer_) {
        std::cerr << "Failed to create text vertex buffer" << std::endl;
        return false;
    }
    
    // Create sampler for text textures
    SDL_GPUSamplerCreateInfo sampler_info = {};
    sampler_info.min_filter = SDL_GPU_FILTER_LINEAR;
    sampler_info.mag_filter = SDL_GPU_FILTER_LINEAR;
    sampler_info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    sampler_info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler_info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler_info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    
    sampler_ = SDL_CreateGPUSampler(device.handle(), &sampler_info);
    if (!sampler_) {
        std::cerr << "Failed to create text sampler: " << SDL_GetError() << std::endl;
        return false;
    }
    
    initialized_ = true;
    return true;
}

void TextRenderer::shutdown() {
    if (sampler_ && device_) {
        SDL_ReleaseGPUSampler(device_->handle(), sampler_);
        sampler_ = nullptr;
    }
    
    vertex_buffer_.reset();
    
    if (font_) {
        TTF_CloseFont(font_);
        font_ = nullptr;
    }
    if (initialized_) {
        TTF_Quit();
        initialized_ = false;
    }
    
    device_ = nullptr;
    pipeline_registry_ = nullptr;
}

void TextRenderer::set_projection(const glm::mat4& projection) {
    projection_ = projection;
}

void TextRenderer::draw_text(SDL_GPUCommandBuffer* cmd, SDL_GPURenderPass* render_pass,
                             const std::string& text, float x, float y, 
                             uint32_t color, float scale) {
    if (!font_ || text.empty() || !device_ || !pipeline_registry_ || !cmd || !render_pass) {
        return;
    }
    
    // Extract color components (ABGR format - same as rest of renderer)
    float r = (color & 0xFF) / 255.0f;
    float g = ((color >> 8) & 0xFF) / 255.0f;
    float b = ((color >> 16) & 0xFF) / 255.0f;
    float a = ((color >> 24) & 0xFF) / 255.0f;
    
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
    
    // Create GPU texture for this text
    SDL_GPUTextureCreateInfo tex_info = {};
    tex_info.type = SDL_GPU_TEXTURETYPE_2D;
    tex_info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    tex_info.width = converted->w;
    tex_info.height = converted->h;
    tex_info.layer_count_or_depth = 1;
    tex_info.num_levels = 1;
    tex_info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    
    SDL_GPUTexture* texture = SDL_CreateGPUTexture(device_->handle(), &tex_info);
    if (!texture) {
        std::cerr << "Failed to create text texture: " << SDL_GetError() << std::endl;
        SDL_DestroySurface(converted);
        return;
    }
    
    // Create transfer buffer and upload texture data
    size_t data_size = static_cast<size_t>(converted->w) * converted->h * 4;
    SDL_GPUTransferBufferCreateInfo transfer_info = {};
    transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transfer_info.size = static_cast<Uint32>(data_size);
    
    SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(device_->handle(), &transfer_info);
    if (!transfer) {
        std::cerr << "Failed to create transfer buffer for text: " << SDL_GetError() << std::endl;
        SDL_ReleaseGPUTexture(device_->handle(), texture);
        SDL_DestroySurface(converted);
        return;
    }
    
    // Map and copy pixel data
    void* mapped = SDL_MapGPUTransferBuffer(device_->handle(), transfer, false);
    if (mapped) {
        memcpy(mapped, converted->pixels, data_size);
        SDL_UnmapGPUTransferBuffer(device_->handle(), transfer);
    }
    
    // Upload to GPU texture
    SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(cmd);
    if (copy_pass) {
        SDL_GPUTextureTransferInfo src = {};
        src.transfer_buffer = transfer;
        src.offset = 0;
        src.pixels_per_row = static_cast<Uint32>(converted->w);
        src.rows_per_layer = static_cast<Uint32>(converted->h);
        
        SDL_GPUTextureRegion dst = {};
        dst.texture = texture;
        dst.w = converted->w;
        dst.h = converted->h;
        dst.d = 1;
        
        SDL_UploadToGPUTexture(copy_pass, &src, &dst, false);
        SDL_EndGPUCopyPass(copy_pass);
    }
    
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
    
    // Upload vertex data
    vertex_buffer_->update(cmd, vertices, sizeof(vertices));
    
    // Bind text pipeline
    auto* text_pipeline = pipeline_registry_->get_text_pipeline();
    if (text_pipeline) {
        text_pipeline->bind(render_pass);
    }
    
    // Push uniforms - projection matrix to vertex shader
    SDL_PushGPUVertexUniformData(cmd, 0, &projection_, sizeof(glm::mat4));
    
    // Push text color to fragment shader
    glm::vec4 text_color(r, g, b, a);
    SDL_PushGPUFragmentUniformData(cmd, 0, &text_color, sizeof(glm::vec4));
    
    // Bind texture and sampler
    SDL_GPUTextureSamplerBinding tex_binding = {};
    tex_binding.texture = texture;
    tex_binding.sampler = sampler_;
    SDL_BindGPUFragmentSamplers(render_pass, 0, &tex_binding, 1);
    
    // Bind vertex buffer
    SDL_GPUBufferBinding vb_binding = {};
    vb_binding.buffer = vertex_buffer_->handle();
    vb_binding.offset = 0;
    SDL_BindGPUVertexBuffers(render_pass, 0, &vb_binding, 1);
    
    // Draw quad
    SDL_DrawGPUPrimitives(render_pass, 6, 1, 0, 0);
    
    // Clean up temporary resources
    // Note: We need to release these after the command buffer is submitted,
    // but for simplicity we'll release them here. The GPU will handle the
    // synchronization since the upload happened in the same command buffer.
    SDL_ReleaseGPUTransferBuffer(device_->handle(), transfer);
    SDL_ReleaseGPUTexture(device_->handle(), texture);
    SDL_DestroySurface(converted);
}

void TextRenderer::draw_text_centered(SDL_GPUCommandBuffer* cmd, SDL_GPURenderPass* render_pass,
                                      const std::string& text, float x, float y, 
                                      uint32_t color, float scale) {
    int width = get_text_width(text, scale);
    draw_text(cmd, render_pass, text, x - width / 2.0f, y, color, scale);
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
