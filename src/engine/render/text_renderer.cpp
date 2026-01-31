#include "text_renderer.hpp"
#include "../gpu/gpu_device.hpp"
#include "../gpu/gpu_buffer.hpp"
#include "../gpu/gpu_pipeline.hpp"
#include "../gpu/pipeline_registry.hpp"
#include "SDL3/SDL_error.h"
#include "SDL3/SDL_gpu.h"
#include "SDL3/SDL_pixels.h"
#include "SDL3/SDL_stdinc.h"
#include "SDL3/SDL_surface.h"
#include "SDL3_ttf/SDL_ttf.h"
#include "glm/ext/matrix_float4x4.hpp"
#include "glm/ext/vector_float4.hpp"
#include <cstdint>
#include <iostream>
#include <cstring>
#include <algorithm>
#include <string>
#include <vector>

namespace mmo::engine::render {

namespace gpu = mmo::engine::gpu;

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
    
    // Create dynamic vertex buffer for text quads (batched)
    // Each vertex: x, y, u, v (4 floats), 6 vertices per quad
    // Support up to MAX_QUEUED_TEXTS quads
    vertex_buffer_ = gpu::GPUBuffer::create_dynamic(
        device,
        gpu::GPUBuffer::Type::Vertex,
        MAX_QUEUED_TEXTS * VERTICES_PER_QUAD * FLOATS_PER_VERTEX * sizeof(float)
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
    // Release any pending GPU resources
    release_pending_resources();

    // Release all cached textures
    if (device_) {
        for (auto& pair : text_cache_) {
            if (pair.second.texture) {
                SDL_ReleaseGPUTexture(device_->handle(), pair.second.texture);
            }
        }
    }
    text_cache_.clear();

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

void TextRenderer::release_pending_resources() {
    if (!device_) return;

    current_frame_++;

    // Release textures from previous frame
    for (SDL_GPUTexture* texture : pending_textures_) {
        if (texture) {
            SDL_ReleaseGPUTexture(device_->handle(), texture);
        }
    }
    pending_textures_.clear();

    // Release transfer buffers from previous frame
    for (SDL_GPUTransferBuffer* transfer : pending_transfers_) {
        if (transfer) {
            SDL_ReleaseGPUTransferBuffer(device_->handle(), transfer);
        }
    }
    pending_transfers_.clear();

    // Clean up old cached textures that haven't been used recently
    for (auto it = text_cache_.begin(); it != text_cache_.end(); ) {
        if (current_frame_ - it->second.last_used_frame > CACHE_EXPIRY_FRAMES) {
            if (it->second.texture) {
                SDL_ReleaseGPUTexture(device_->handle(), it->second.texture);
            }
            it = text_cache_.erase(it);
        } else {
            ++it;
        }
    }
}

TextRenderer::CachedText* TextRenderer::get_or_create_text_texture(
    SDL_GPUCommandBuffer* cmd, const std::string& text, bool in_render_pass) {

    // Check cache first
    auto it = text_cache_.find(text);
    if (it != text_cache_.end()) {
        it->second.last_used_frame = current_frame_;
        return &it->second;
    }

    // If we're in a render pass, we can't create textures (requires copy pass)
    if (in_render_pass) {
        return nullptr;
    }

    // Create new texture
    if (!font_ || !device_ || !cmd) {
        return nullptr;
    }

    SDL_Color sdl_color = {255, 255, 255, 255};
    SDL_Surface* surface = TTF_RenderText_Blended(font_, text.c_str(), 0, sdl_color);
    if (!surface) {
        return nullptr;
    }

    SDL_Surface* converted = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(surface);
    if (!converted) {
        return nullptr;
    }

    // Create GPU texture
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
        SDL_DestroySurface(converted);
        return nullptr;
    }

    // Create transfer buffer
    size_t data_size = static_cast<size_t>(converted->w) * converted->h * 4;
    SDL_GPUTransferBufferCreateInfo transfer_info = {};
    transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transfer_info.size = static_cast<Uint32>(data_size);

    SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(device_->handle(), &transfer_info);
    if (!transfer) {
        SDL_ReleaseGPUTexture(device_->handle(), texture);
        SDL_DestroySurface(converted);
        return nullptr;
    }

    // Map and copy pixel data
    void* mapped = SDL_MapGPUTransferBuffer(device_->handle(), transfer, false);
    if (!mapped) {
        SDL_ReleaseGPUTransferBuffer(device_->handle(), transfer);
        SDL_ReleaseGPUTexture(device_->handle(), texture);
        SDL_DestroySurface(converted);
        return nullptr;
    }

    memcpy(mapped, converted->pixels, data_size);
    SDL_UnmapGPUTransferBuffer(device_->handle(), transfer);

    // Upload to GPU texture (copy pass - only valid outside render pass)
    SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(cmd);
    if (!copy_pass) {
        SDL_ReleaseGPUTransferBuffer(device_->handle(), transfer);
        SDL_ReleaseGPUTexture(device_->handle(), texture);
        SDL_DestroySurface(converted);
        return nullptr;
    }

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

    // Defer transfer buffer cleanup
    pending_transfers_.push_back(transfer);

    // Store in cache
    CachedText cached;
    cached.texture = texture;
    cached.width = converted->w;
    cached.height = converted->h;
    cached.last_used_frame = current_frame_;

    SDL_DestroySurface(converted);

    auto result = text_cache_.emplace(text, cached);
    return &result.first->second;
}

void TextRenderer::prepare_text_textures(SDL_GPUCommandBuffer* cmd, const std::vector<std::string>& texts) {
    for (const auto& text : texts) {
        if (!text.empty()) {
            get_or_create_text_texture(cmd, text, false);
        }
    }
}

void TextRenderer::create_pending_textures(SDL_GPUCommandBuffer* cmd) {
    if (pending_text_creates_.empty()) return;

    // Create textures for all pending text strings
    for (const auto& text : pending_text_creates_) {
        get_or_create_text_texture(cmd, text, false);
    }
    pending_text_creates_.clear();
}

void TextRenderer::draw_text(SDL_GPUCommandBuffer* cmd, SDL_GPURenderPass* render_pass,
                             const std::string& text, float x, float y,
                             uint32_t color, float scale) {
    if (!font_ || text.empty() || !device_ || !pipeline_registry_ || !cmd || !render_pass) {
        return;
    }

    // Try to get cached texture (passing true = we're in a render pass, can't create new)
    CachedText* cached = get_or_create_text_texture(cmd, text, true);
    if (!cached || !cached->texture) {
        // Text not in cache yet - queue it for creation after render passes end
        // Check if not already queued
        if (std::find(pending_text_creates_.begin(), pending_text_creates_.end(), text) == pending_text_creates_.end()) {
            pending_text_creates_.push_back(text);
        }
        return;
    }

    // Extract color components (ABGR format - same as rest of renderer)
    float r = (color & 0xFF) / 255.0f;
    float g = ((color >> 8) & 0xFF) / 255.0f;
    float b = ((color >> 16) & 0xFF) / 255.0f;
    float a = ((color >> 24) & 0xFF) / 255.0f;

    // Calculate dimensions
    float w = cached->width * scale;
    float h = cached->height * scale;

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
    tex_binding.texture = cached->texture;
    tex_binding.sampler = sampler_;
    SDL_BindGPUFragmentSamplers(render_pass, 0, &tex_binding, 1);

    // Bind vertex buffer
    SDL_GPUBufferBinding vb_binding = {};
    vb_binding.buffer = vertex_buffer_->handle();
    vb_binding.offset = 0;
    SDL_BindGPUVertexBuffers(render_pass, 0, &vb_binding, 1);

    // Draw quad
    SDL_DrawGPUPrimitives(render_pass, 6, 1, 0, 0);
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

void TextRenderer::queue_text_draw(const std::string& text, float x, float y,
                                    uint32_t color, float scale) {
    if (!font_ || text.empty()) return;
    if (queued_texts_.size() >= MAX_QUEUED_TEXTS) return;

    // Check if text is cached - if not, queue for texture creation
    auto it = text_cache_.find(text);
    if (it == text_cache_.end()) {
        if (std::find(pending_text_creates_.begin(), pending_text_creates_.end(), text)
            == pending_text_creates_.end()) {
            pending_text_creates_.push_back(text);
        }
        return;  // Text will be rendered next frame after texture is created
    }

    CachedText& cached = it->second;
    cached.last_used_frame = current_frame_;

    // Calculate vertex offset in batch
    size_t vertex_offset = batch_vertices_.size();

    // Calculate dimensions
    float w = cached.width * scale;
    float h = cached.height * scale;

    // Add quad vertices (pos.x, pos.y, tex.x, tex.y)
    float vertices[] = {
        x,     y,     0.0f, 0.0f,
        x + w, y,     1.0f, 0.0f,
        x + w, y + h, 1.0f, 1.0f,
        x,     y,     0.0f, 0.0f,
        x + w, y + h, 1.0f, 1.0f,
        x,     y + h, 0.0f, 1.0f
    };

    // Add vertices to batch
    for (float v : vertices) {
        batch_vertices_.push_back(v);
    }

    // Queue the text draw
    QueuedText qt;
    qt.text = text;
    qt.x = x;
    qt.y = y;
    qt.color = color;
    qt.scale = scale;
    qt.vertex_offset = vertex_offset;
    queued_texts_.push_back(qt);
}

void TextRenderer::upload_queued_text(SDL_GPUCommandBuffer* cmd) {
    if (batch_vertices_.empty() || !vertex_buffer_ || !cmd) return;

    // Upload all batched vertex data in one copy pass
    vertex_buffer_->update(cmd, batch_vertices_.data(),
                           batch_vertices_.size() * sizeof(float));
}

void TextRenderer::draw_queued_text(SDL_GPUCommandBuffer* cmd, SDL_GPURenderPass* render_pass) {
    if (queued_texts_.empty() || !cmd || !render_pass || !pipeline_registry_) return;

    for (const auto& qt : queued_texts_) {
        // Get cached texture
        auto it = text_cache_.find(qt.text);
        if (it == text_cache_.end() || !it->second.texture) continue;

        CachedText& cached = it->second;

        // Extract color components (ABGR format)
        float r = (qt.color & 0xFF) / 255.0f;
        float g = ((qt.color >> 8) & 0xFF) / 255.0f;
        float b = ((qt.color >> 16) & 0xFF) / 255.0f;
        float a = ((qt.color >> 24) & 0xFF) / 255.0f;

        // Bind text pipeline
        auto* text_pipeline = pipeline_registry_->get_text_pipeline();
        if (text_pipeline) {
            text_pipeline->bind(render_pass);
        }

        // Push uniforms
        SDL_PushGPUVertexUniformData(cmd, 0, &projection_, sizeof(glm::mat4));
        glm::vec4 text_color(r, g, b, a);
        SDL_PushGPUFragmentUniformData(cmd, 0, &text_color, sizeof(glm::vec4));

        // Bind texture and sampler
        SDL_GPUTextureSamplerBinding tex_binding = {};
        tex_binding.texture = cached.texture;
        tex_binding.sampler = sampler_;
        SDL_BindGPUFragmentSamplers(render_pass, 0, &tex_binding, 1);

        // Bind vertex buffer at the correct offset
        SDL_GPUBufferBinding vb_binding = {};
        vb_binding.buffer = vertex_buffer_->handle();
        vb_binding.offset = static_cast<Uint32>(qt.vertex_offset * sizeof(float));
        SDL_BindGPUVertexBuffers(render_pass, 0, &vb_binding, 1);

        // Draw quad
        SDL_DrawGPUPrimitives(render_pass, 6, 1, 0, 0);
    }

    // Clear queued data for next frame
    queued_texts_.clear();
    batch_vertices_.clear();
}

void TextRenderer::draw_text_immediate(SDL_GPUCommandBuffer* cmd, SDL_GPURenderPass* render_pass,
                                        const std::string& text, float x, float y,
                                        uint32_t color, float scale) {
    // For immediate mode, we use the queued text system but process just this one
    // This assumes upload_queued_text was already called for any previously queued text

    if (!font_ || text.empty() || !device_ || !pipeline_registry_ || !cmd || !render_pass) {
        return;
    }

    // Get cached texture
    auto it = text_cache_.find(text);
    if (it == text_cache_.end() || !it->second.texture) {
        // Queue for next frame
        if (std::find(pending_text_creates_.begin(), pending_text_creates_.end(), text)
            == pending_text_creates_.end()) {
            pending_text_creates_.push_back(text);
        }
        return;
    }

    CachedText& cached = it->second;
    cached.last_used_frame = current_frame_;

    // Extract color components
    float r = (color & 0xFF) / 255.0f;
    float g = ((color >> 8) & 0xFF) / 255.0f;
    float b = ((color >> 16) & 0xFF) / 255.0f;
    float a = ((color >> 24) & 0xFF) / 255.0f;

    // Calculate dimensions
    float w = cached.width * scale;
    float h = cached.height * scale;

    // Build quad vertices
    float vertices[] = {
        x,     y,     0.0f, 0.0f,
        x + w, y,     1.0f, 0.0f,
        x + w, y + h, 1.0f, 1.0f,
        x,     y,     0.0f, 0.0f,
        x + w, y + h, 1.0f, 1.0f,
        x,     y + h, 0.0f, 1.0f
    };

    // For immediate mode, we need to upload this single quad
    // Since we're in a render pass, we can't do copy pass - so we use
    // a pre-allocated section of the vertex buffer

    // Use the end of batch_vertices_ for immediate draws
    size_t immediate_offset = batch_vertices_.size();
    for (float v : vertices) {
        batch_vertices_.push_back(v);
    }

    // This is a problem - we can't upload during render pass!
    // For now, we just draw if data was already uploaded via queue_text_draw
    // The caller should use queue_text_draw + upload_queued_text + draw_queued_text instead

    // Bind text pipeline
    auto* text_pipeline = pipeline_registry_->get_text_pipeline();
    if (text_pipeline) {
        text_pipeline->bind(render_pass);
    }

    // Push uniforms
    SDL_PushGPUVertexUniformData(cmd, 0, &projection_, sizeof(glm::mat4));
    glm::vec4 text_color(r, g, b, a);
    SDL_PushGPUFragmentUniformData(cmd, 0, &text_color, sizeof(glm::vec4));

    // Bind texture and sampler
    SDL_GPUTextureSamplerBinding tex_binding = {};
    tex_binding.texture = cached.texture;
    tex_binding.sampler = sampler_;
    SDL_BindGPUFragmentSamplers(render_pass, 0, &tex_binding, 1);

    // Note: This will only work if vertices were pre-uploaded
    // For true immediate mode, we'd need a mapped persistent buffer
    SDL_GPUBufferBinding vb_binding = {};
    vb_binding.buffer = vertex_buffer_->handle();
    vb_binding.offset = static_cast<Uint32>(immediate_offset * sizeof(float));
    SDL_BindGPUVertexBuffers(render_pass, 0, &vb_binding, 1);

    SDL_DrawGPUPrimitives(render_pass, 6, 1, 0, 0);
}

} // namespace mmo::engine::render
