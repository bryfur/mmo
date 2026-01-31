#include "ui_renderer.hpp"
#include "SDL3/SDL_gpu.h"
#include "SDL3/SDL_log.h"
#include "SDL3/SDL_rect.h"
#include "glm/ext/matrix_clip_space.hpp"
#include "glm/ext/matrix_float4x4.hpp"
#include "glm/ext/vector_float4.hpp"
#include "text_renderer.hpp"
#include "../gpu/gpu_device.hpp"
#include "../gpu/gpu_buffer.hpp"
#include "../gpu/gpu_pipeline.hpp"
#include "../gpu/gpu_uniforms.hpp"
#include "../gpu/pipeline_registry.hpp"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <cmath>
#include <iostream>
#include <cstdio>

namespace mmo::engine::render {

namespace gpu = mmo::engine::gpu;

UIRenderer::UIRenderer() = default;

UIRenderer::~UIRenderer() {
    shutdown();
}

bool UIRenderer::init(gpu::GPUDevice& device, gpu::PipelineRegistry& pipeline_registry, 
                      int width, int height) {
    device_ = &device;
    pipeline_registry_ = &pipeline_registry;
    width_ = width;
    height_ = height;
    
    // Create dynamic vertex buffer for UI quads
    // Each vertex matches Vertex2D layout: x, y, u, v, r, g, b, a (8 floats)
    vertex_buffer_ = gpu::GPUBuffer::create_dynamic(
        device, 
        gpu::GPUBuffer::Type::Vertex, 
        MAX_VERTICES * sizeof(UIVertex)
    );
    
    if (!vertex_buffer_) {
        std::cerr << "Failed to create UI vertex buffer" << std::endl;
        return false;
    }
    
    // Reserve space for vertex batch
    vertex_batch_.reserve(MAX_VERTICES);

    // Create dummy 1x1 white texture for when no texture is needed
    // (SDL3 GPU requires all sampler bindings to be valid)
    {
        SDL_GPUTextureCreateInfo tex_info = {};
        tex_info.type = SDL_GPU_TEXTURETYPE_2D;
        tex_info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        tex_info.width = 1;
        tex_info.height = 1;
        tex_info.layer_count_or_depth = 1;
        tex_info.num_levels = 1;
        tex_info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;

        dummy_texture_ = SDL_CreateGPUTexture(device.handle(), &tex_info);
        if (!dummy_texture_) {
            std::cerr << "Failed to create UI dummy texture" << std::endl;
            return false;
        }

        // Upload white pixel
        SDL_GPUTransferBufferCreateInfo tb_info = {};
        tb_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        tb_info.size = 4;
        SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(device.handle(), &tb_info);
        if (tb) {
            uint8_t* data = static_cast<uint8_t*>(SDL_MapGPUTransferBuffer(device.handle(), tb, false));
            if (data) {
                data[0] = 255; data[1] = 255; data[2] = 255; data[3] = 255;
                SDL_UnmapGPUTransferBuffer(device.handle(), tb);

                SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device.handle());
                if (cmd) {
                    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
                    if (copy) {
                        SDL_GPUTextureTransferInfo src = {};
                        src.transfer_buffer = tb;
                        src.offset = 0;

                        SDL_GPUTextureRegion dst = {};
                        dst.texture = dummy_texture_;
                        dst.w = 1;
                        dst.h = 1;
                        dst.d = 1;

                        SDL_UploadToGPUTexture(copy, &src, &dst, false);
                        SDL_EndGPUCopyPass(copy);
                    }
                    SDL_SubmitGPUCommandBuffer(cmd);
                }
            }
            SDL_ReleaseGPUTransferBuffer(device.handle(), tb);
        }

        // Create sampler
        SDL_GPUSamplerCreateInfo samp_info = {};
        samp_info.min_filter = SDL_GPU_FILTER_NEAREST;
        samp_info.mag_filter = SDL_GPU_FILTER_NEAREST;
        samp_info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
        samp_info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        samp_info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        samp_info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;

        dummy_sampler_ = SDL_CreateGPUSampler(device.handle(), &samp_info);
        if (!dummy_sampler_) {
            std::cerr << "Failed to create UI dummy sampler" << std::endl;
            return false;
        }
    }

    // Initialize text renderer
    text_renderer_ = std::make_unique<TextRenderer>();
    if (text_renderer_->init(device, pipeline_registry)) {
        std::cout << "UI text renderer initialized" << std::endl;
    } else {
        std::cerr << "Failed to initialize text renderer" << std::endl;
    }

    // Set up projection
    set_screen_size(width, height);

    return true;
}

void UIRenderer::shutdown() {
    vertex_buffer_.reset();

    if (device_) {
        if (dummy_sampler_) {
            SDL_ReleaseGPUSampler(device_->handle(), dummy_sampler_);
            dummy_sampler_ = nullptr;
        }
        if (dummy_texture_) {
            SDL_ReleaseGPUTexture(device_->handle(), dummy_texture_);
            dummy_texture_ = nullptr;
        }
    }

    if (text_renderer_) {
        text_renderer_->shutdown();
        text_renderer_.reset();
    }

    device_ = nullptr;
    pipeline_registry_ = nullptr;
}

void UIRenderer::set_screen_size(int width, int height) {
    width_ = width;
    height_ = height;
    // Orthographic projection for SDL3 GPU (Vulkan-style coordinates):
    // Screen coordinates: (0,0) at top-left, Y increases downward.
    // Vulkan clip space: Y=+1 at top, Y=-1 at bottom (maps to framebuffer top/bottom).
    // To map screen Y=0 to clip Y=+1 (top), use glm::ortho with bottom=height, top=0.
    // This flips the Y axis so screen coords map correctly to Vulkan clip space.
    projection_ = glm::ortho(0.0f, static_cast<float>(width),
                              static_cast<float>(height), 0.0f, -1.0f, 1.0f);
}

void UIRenderer::begin(SDL_GPUCommandBuffer* cmd) {
    current_cmd_ = cmd;
    current_pass_ = nullptr;  // No render pass during recording phase
    vertex_batch_.clear();
    queued_text_draws_.clear();

    // Release pending GPU resources from previous frame
    if (text_renderer_) {
        text_renderer_->release_pending_resources();
    }

    if (text_renderer_ && text_renderer_->is_ready()) {
        text_renderer_->set_projection(projection_);
    }
}

void UIRenderer::end() {
    // Recording phase ends - don't flush yet, wait for execute()
    // current_cmd_ is kept for execute() phase
}

void UIRenderer::execute(SDL_GPUCommandBuffer* cmd, SDL_GPUTexture* swapchain, bool clear_background) {
    if (!cmd || !swapchain) {
        return;
    }

    // Phase 1: Queue all text draws for batched rendering
    if (text_renderer_ && text_renderer_->is_ready()) {
        for (const auto& td : queued_text_draws_) {
            text_renderer_->queue_text_draw(td.text, td.x, td.y, td.color, td.scale);
        }
    }

    // Phase 2: Upload all data (copy passes - BEFORE render pass)
    // Upload UI vertex data
    if (!vertex_batch_.empty() && vertex_buffer_) {
        vertex_buffer_->update(cmd, vertex_batch_.data(),
                               vertex_batch_.size() * sizeof(UIVertex));
    }

    // Create any pending text textures (copy pass)
    if (text_renderer_) {
        text_renderer_->create_pending_textures(cmd);
    }

    // Upload queued text vertex data (copy pass)
    if (text_renderer_ && text_renderer_->is_ready()) {
        text_renderer_->upload_queued_text(cmd);
    }

    // Phase 3: Start UI render pass
    SDL_GPUColorTargetInfo color_target = {};
    color_target.texture = swapchain;
    // If no 3D pass happened (menu state), clear to dark background
    // Otherwise preserve existing 3D content
    color_target.load_op = clear_background ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
    color_target.store_op = SDL_GPU_STOREOP_STORE;
    color_target.clear_color = { 0.1f, 0.1f, 0.15f, 1.0f };  // Dark menu background

    SDL_GPURenderPass* render_pass = SDL_BeginGPURenderPass(cmd, &color_target, 1, nullptr);
    if (!render_pass) {
        std::cerr << "UIRenderer::execute: Failed to begin render pass" << std::endl;
        return;
    }

    // Set viewport and scissor to full screen
    SDL_GPUViewport viewport = {
        0.0f, 0.0f,
        static_cast<float>(width_), static_cast<float>(height_),
        0.0f, 1.0f
    };
    SDL_SetGPUViewport(render_pass, &viewport);

    SDL_Rect scissor = { 0, 0, width_, height_ };
    SDL_SetGPUScissor(render_pass, &scissor);

    // Bind UI pipeline - MUST succeed before drawing
    gpu::GPUPipeline* ui_pipeline = nullptr;
    if (pipeline_registry_) {
        ui_pipeline = pipeline_registry_->get_ui_pipeline();
    }

    if (!ui_pipeline) {
        SDL_Log("UIRenderer::execute: UI pipeline not available, skipping UI draw");
        SDL_EndGPURenderPass(render_pass);
        vertex_batch_.clear();
        queued_text_draws_.clear();
        current_cmd_ = nullptr;
        current_pass_ = nullptr;
        return;
    }

    ui_pipeline->bind(render_pass);

    // Push vertex uniform (screen size for 2D transformation)
    gpu::UIScreenUniforms screen_uniforms = {};
    screen_uniforms.width = static_cast<float>(width_);
    screen_uniforms.height = static_cast<float>(height_);
    SDL_PushGPUVertexUniformData(cmd, 0, &screen_uniforms, sizeof(screen_uniforms));

    // Push fragment uniform (has_texture = 0 for solid colors)
    gpu::UIFragmentUniforms frag_uniforms = {};
    frag_uniforms.hasTexture = 0;
    SDL_PushGPUFragmentUniformData(cmd, 0, &frag_uniforms, sizeof(frag_uniforms));

    // Bind dummy texture/sampler for fragment shader (required even when not using textures)
    if (dummy_texture_ && dummy_sampler_) {
        SDL_GPUTextureSamplerBinding sampler_binding = {};
        sampler_binding.texture = dummy_texture_;
        sampler_binding.sampler = dummy_sampler_;
        SDL_BindGPUFragmentSamplers(render_pass, 0, &sampler_binding, 1);
    }

    // Phase 3a: Draw UI primitives (vertices already uploaded)
    if (!vertex_batch_.empty() && vertex_buffer_) {
        SDL_GPUBufferBinding vb_binding = {};
        vb_binding.buffer = vertex_buffer_->handle();
        vb_binding.offset = 0;
        SDL_BindGPUVertexBuffers(render_pass, 0, &vb_binding, 1);
        SDL_DrawGPUPrimitives(render_pass, static_cast<uint32_t>(vertex_batch_.size()), 1, 0, 0);
    }

    // Phase 3b: Draw queued text (vertices already uploaded)
    if (text_renderer_ && text_renderer_->is_ready()) {
        text_renderer_->draw_queued_text(cmd, render_pass);

        // Re-bind UI pipeline after text (in case more UI drawing needed later)
        if (pipeline_registry_) {
            auto* ui_pipeline = pipeline_registry_->get_ui_pipeline();
            if (ui_pipeline) {
                ui_pipeline->bind(render_pass);
            }
        }
        // Re-push uniforms after re-bind
        gpu::UIScreenUniforms screen_uniforms2 = {};
        screen_uniforms2.width = static_cast<float>(width_);
        screen_uniforms2.height = static_cast<float>(height_);
        SDL_PushGPUVertexUniformData(cmd, 0, &screen_uniforms2, sizeof(screen_uniforms2));

        gpu::UIFragmentUniforms frag_uniforms2 = {};
        frag_uniforms2.hasTexture = 0;
        SDL_PushGPUFragmentUniformData(cmd, 0, &frag_uniforms2, sizeof(frag_uniforms2));
        // Re-bind dummy sampler after text
        if (dummy_texture_ && dummy_sampler_) {
            SDL_GPUTextureSamplerBinding sampler_binding = {};
            sampler_binding.texture = dummy_texture_;
            sampler_binding.sampler = dummy_sampler_;
            SDL_BindGPUFragmentSamplers(render_pass, 0, &sampler_binding, 1);
        }
    }

    // End render pass
    SDL_EndGPURenderPass(render_pass);

    // Clear for next frame
    vertex_batch_.clear();
    queued_text_draws_.clear();
    current_cmd_ = nullptr;
    current_pass_ = nullptr;
}

void UIRenderer::flush_batch() {
    // No-op during recording phase (current_pass_ is null)
    // All upload and drawing happens in execute()
    //
    // This method is kept for compatibility with code that checks batch capacity,
    // but the actual flush is now done in execute().
}

glm::vec4 UIRenderer::color_from_uint32(uint32_t color) const {
    // Color format is ARGB: 0xAARRGGBB
    float a = ((color >> 24) & 0xFF) / 255.0f;
    float r = ((color >> 16) & 0xFF) / 255.0f;
    float g = ((color >> 8) & 0xFF) / 255.0f;
    float b = (color & 0xFF) / 255.0f;
    return glm::vec4(r, g, b, a);
}

void UIRenderer::draw_quad(float x, float y, float w, float h, const glm::vec4& color) {
    // Check if we need to flush before adding more vertices
    if (vertex_batch_.size() + 6 > MAX_VERTICES) {
        flush_batch();
        
        // Re-bind UI pipeline after flush
        if (pipeline_registry_) {
            auto* ui_pipeline = pipeline_registry_->get_ui_pipeline();
            if (ui_pipeline && current_pass_) {
                ui_pipeline->bind(current_pass_);
            }
        }
        // Re-push projection matrix
        if (current_cmd_) {
            SDL_PushGPUVertexUniformData(current_cmd_, 0, &projection_, sizeof(glm::mat4));
        }
    }
    
    // Add 6 vertices for two triangles (quad)
    UIVertex v0 = {x, y, 0, 0, color.r, color.g, color.b, color.a};
    UIVertex v1 = {x + w, y, 0, 0, color.r, color.g, color.b, color.a};
    UIVertex v2 = {x + w, y + h, 0, 0, color.r, color.g, color.b, color.a};
    UIVertex v3 = {x, y + h, 0, 0, color.r, color.g, color.b, color.a};
    
    // Triangle 1
    vertex_batch_.push_back(v0);
    vertex_batch_.push_back(v1);
    vertex_batch_.push_back(v2);
    
    // Triangle 2
    vertex_batch_.push_back(v0);
    vertex_batch_.push_back(v2);
    vertex_batch_.push_back(v3);
}

void UIRenderer::draw_filled_rect(float x, float y, float w, float h, uint32_t color) {
    draw_quad(x, y, w, h, color_from_uint32(color));
}

void UIRenderer::draw_rect_outline(float x, float y, float w, float h, uint32_t color, float line_width) {
    glm::vec4 c = color_from_uint32(color);
    draw_quad(x, y, w, line_width, c);  // Top
    draw_quad(x, y + h - line_width, w, line_width, c);  // Bottom
    draw_quad(x, y, line_width, h, c);  // Left
    draw_quad(x + w - line_width, y, line_width, h, c);  // Right
}

void UIRenderer::draw_circle(float x, float y, float radius, uint32_t color, int segments) {
    glm::vec4 c = color_from_uint32(color);
    
    // Check if we have room for all circle vertices
    size_t vertices_needed = static_cast<size_t>(segments) * 3;
    if (vertex_batch_.size() + vertices_needed > MAX_VERTICES) {
        flush_batch();
        
        // Re-bind pipeline after flush
        if (pipeline_registry_ && current_pass_) {
            auto* ui_pipeline = pipeline_registry_->get_ui_pipeline();
            if (ui_pipeline) {
                ui_pipeline->bind(current_pass_);
            }
        }
        if (current_cmd_) {
            SDL_PushGPUVertexUniformData(current_cmd_, 0, &projection_, sizeof(glm::mat4));
        }
    }
    
    constexpr float PI = 3.14159265359f;
    for (int i = 0; i < segments; ++i) {
        float a1 = (i / static_cast<float>(segments)) * 2.0f * PI;
        float a2 = ((i + 1) / static_cast<float>(segments)) * 2.0f * PI;
        
        UIVertex center = {x, y, 0, 0, c.r, c.g, c.b, c.a};
        UIVertex p1 = {x + std::cos(a1) * radius, y + std::sin(a1) * radius, 0, 0, c.r, c.g, c.b, c.a};
        UIVertex p2 = {x + std::cos(a2) * radius, y + std::sin(a2) * radius, 0, 0, c.r, c.g, c.b, c.a};
        
        vertex_batch_.push_back(center);
        vertex_batch_.push_back(p1);
        vertex_batch_.push_back(p2);
    }
}

void UIRenderer::draw_circle_outline(float x, float y, float radius, uint32_t color, 
                                      float line_width, int segments) {
    if (line_width <= 0.0f || radius <= 0.0f || segments <= 0) {
        return;
    }

    float outer_radius = radius;
    float inner_radius = radius - line_width;
    if (inner_radius <= 0.0f) {
        // If the line width is too large, fall back to filled circle
        draw_circle(x, y, outer_radius, color, segments);
        return;
    }

    glm::vec4 c = color_from_uint32(color);

    // Each segment of the ring is two triangles (6 vertices)
    size_t vertices_needed = static_cast<size_t>(segments) * 6;
    if (vertex_batch_.size() + vertices_needed > MAX_VERTICES) {
        flush_batch();

        // Re-bind pipeline after flush
        if (pipeline_registry_ && current_pass_) {
            auto* ui_pipeline = pipeline_registry_->get_ui_pipeline();
            if (ui_pipeline) {
                ui_pipeline->bind(current_pass_);
            }
        }
        if (current_cmd_) {
            SDL_PushGPUVertexUniformData(current_cmd_, 0, &projection_, sizeof(glm::mat4));
        }
    }

    constexpr float PI = 3.14159265359f;
    for (int i = 0; i < segments; ++i) {
        float t1 = i / static_cast<float>(segments);
        float t2 = (i + 1) / static_cast<float>(segments);
        float a1 = t1 * 2.0f * PI;
        float a2 = t2 * 2.0f * PI;

        float cos_a1 = std::cos(a1);
        float sin_a1 = std::sin(a1);
        float cos_a2 = std::cos(a2);
        float sin_a2 = std::sin(a2);

        // Outer edge points
        UIVertex outer1 = {x + cos_a1 * outer_radius, y + sin_a1 * outer_radius,
                           0, 0, c.r, c.g, c.b, c.a};
        UIVertex outer2 = {x + cos_a2 * outer_radius, y + sin_a2 * outer_radius,
                           0, 0, c.r, c.g, c.b, c.a};

        // Inner edge points
        UIVertex inner1 = {x + cos_a1 * inner_radius, y + sin_a1 * inner_radius,
                           0, 0, c.r, c.g, c.b, c.a};
        UIVertex inner2 = {x + cos_a2 * inner_radius, y + sin_a2 * inner_radius,
                           0, 0, c.r, c.g, c.b, c.a};

        // First triangle (outer1, outer2, inner1)
        vertex_batch_.push_back(outer1);
        vertex_batch_.push_back(outer2);
        vertex_batch_.push_back(inner1);

        // Second triangle (outer2, inner2, inner1)
        vertex_batch_.push_back(outer2);
        vertex_batch_.push_back(inner2);
        vertex_batch_.push_back(inner1);
    }
}

void UIRenderer::draw_line(float x1, float y1, float x2, float y2, uint32_t color, float line_width) {
    glm::vec4 c = color_from_uint32(color);
    
    float dx = x2 - x1;
    float dy = y2 - y1;
    float len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.001f) return;
    
    // Calculate perpendicular offset for line width
    float nx = -dy / len * line_width / 2;
    float ny = dx / len * line_width / 2;
    
    // Check if we need to flush
    if (vertex_batch_.size() + 6 > MAX_VERTICES) {
        flush_batch();
        
        if (pipeline_registry_ && current_pass_) {
            auto* ui_pipeline = pipeline_registry_->get_ui_pipeline();
            if (ui_pipeline) {
                ui_pipeline->bind(current_pass_);
            }
        }
        if (current_cmd_) {
            SDL_PushGPUVertexUniformData(current_cmd_, 0, &projection_, sizeof(glm::mat4));
        }
    }
    
    // Create line quad vertices
    UIVertex v0 = {x1 + nx, y1 + ny, 0, 0, c.r, c.g, c.b, c.a};
    UIVertex v1 = {x1 - nx, y1 - ny, 0, 0, c.r, c.g, c.b, c.a};
    UIVertex v2 = {x2 - nx, y2 - ny, 0, 0, c.r, c.g, c.b, c.a};
    UIVertex v3 = {x2 + nx, y2 + ny, 0, 0, c.r, c.g, c.b, c.a};
    
    // Triangle 1
    vertex_batch_.push_back(v0);
    vertex_batch_.push_back(v1);
    vertex_batch_.push_back(v2);
    
    // Triangle 2
    vertex_batch_.push_back(v0);
    vertex_batch_.push_back(v2);
    vertex_batch_.push_back(v3);
}

void UIRenderer::draw_text(const std::string& text, float x, float y, uint32_t color, float scale) {
    if (!text.empty()) {
        // Queue text draw for later execution (during execute() phase)
        QueuedTextDraw td;
        td.text = text;
        td.x = x;
        td.y = y;
        td.color = color;
        td.scale = scale;
        queued_text_draws_.push_back(td);
    }
}

void UIRenderer::draw_button(float x, float y, float w, float h, const std::string& label,
                              uint32_t color, bool selected) {
    draw_filled_rect(x, y, w, h, color);
    uint32_t border_color = selected ? 0xFFFFFFFF : 0xFF888888;
    draw_rect_outline(x, y, w, h, border_color, selected ? 3.0f : 2.0f);

    if (text_renderer_ && text_renderer_->is_ready() && !label.empty()) {
        // Calculate centered text position and queue for later rendering
        int text_w = text_renderer_->get_text_width(label, 1.0f);
        int text_h = text_renderer_->get_text_height(1.0f);
        float text_x = x + (w - text_w) / 2.0f;
        float text_y = y + (h - text_h) / 2.0f;
        draw_text(label, text_x, text_y, 0xFFFFFFFF, 1.0f);
    }
}

} // namespace mmo::engine::render
