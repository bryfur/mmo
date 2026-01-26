#pragma once

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <glm/glm.hpp>
#include <string>
#include <memory>
#include <vector>

// Forward declarations
namespace mmo::gpu {
    class GPUDevice;
    class GPUBuffer;
    class GPUTexture;
    class PipelineRegistry;
}

namespace mmo {

/**
 * TextRenderer handles text rendering using SDL_ttf and SDL3 GPU API.
 * 
 * SDL3 GPU Migration: This renderer now uses GPUBuffer for vertex data,
 * GPUTexture for font rendering, and the text pipeline from PipelineRegistry.
 * Text is rendered by creating temporary textures from TTF surfaces.
 */
class TextRenderer {
public:
    TextRenderer();
    ~TextRenderer();
    
    /**
     * Initialize text rendering resources.
     * @param device GPU device for creating resources
     * @param pipeline_registry Registry for accessing text pipeline
     */
    bool init(gpu::GPUDevice& device, gpu::PipelineRegistry& pipeline_registry);
    void shutdown();
    
    // Set projection matrix before drawing
    void set_projection(const glm::mat4& projection);
    
    /**
     * Draw text at the specified position.
     * @param cmd Command buffer for the frame
     * @param render_pass Active render pass to draw into
     * @param text The text string to render
     * @param x X position (screen coordinates)
     * @param y Y position (screen coordinates)
     * @param color Text color in ABGR format
     * @param scale Text scale factor
     */
    void draw_text(SDL_GPUCommandBuffer* cmd, SDL_GPURenderPass* render_pass,
                   const std::string& text, float x, float y, 
                   uint32_t color = 0xFFFFFFFF, float scale = 1.0f);
    
    void draw_text_centered(SDL_GPUCommandBuffer* cmd, SDL_GPURenderPass* render_pass,
                            const std::string& text, float x, float y, 
                            uint32_t color = 0xFFFFFFFF, float scale = 1.0f);
    
    int get_text_width(const std::string& text, float scale = 1.0f);
    int get_text_height(float scale = 1.0f);
    
    bool is_ready() const { return initialized_ && font_ != nullptr; }
    
    /**
     * Release GPU resources from previous frames.
     * Call this at the beginning of each frame to clean up resources
     * that are no longer in use by the GPU.
     */
    void release_pending_resources();
    
private:
    TTF_Font* font_ = nullptr;
    int font_size_ = 18;
    bool initialized_ = false;
    
    gpu::GPUDevice* device_ = nullptr;
    gpu::PipelineRegistry* pipeline_registry_ = nullptr;
    glm::mat4 projection_;
    
    // Dynamic vertex buffer for text quads
    std::unique_ptr<gpu::GPUBuffer> vertex_buffer_;
    
    // Sampler for text textures
    SDL_GPUSampler* sampler_ = nullptr;
    
    // Pending GPU resources to be released after GPU has finished using them
    // We use double-buffering: resources go to pending_*, then released next frame
    std::vector<SDL_GPUTexture*> pending_textures_;
    std::vector<SDL_GPUTransferBuffer*> pending_transfers_;
};

} // namespace mmo
