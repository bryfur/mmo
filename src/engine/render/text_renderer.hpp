#pragma once

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <glm/glm.hpp>
#include <string>
#include <memory>
#include <vector>
#include <unordered_map>

// Forward declarations
namespace mmo::engine::gpu {
    class GPUDevice;
    class GPUBuffer;
    class GPUTexture;
    class PipelineRegistry;
}

namespace mmo::engine::render {

namespace gpu = mmo::engine::gpu;

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

    /**
     * Queue a text draw for batched rendering.
     * Call this instead of draw_text during recording phase.
     */
    void queue_text_draw(const std::string& text, float x, float y,
                         uint32_t color = 0xFFFFFFFF, float scale = 1.0f);

    /**
     * Upload all queued text vertex data. Call BEFORE starting render pass.
     * This performs copy pass operations.
     */
    void upload_queued_text(SDL_GPUCommandBuffer* cmd);

    /**
     * Draw all queued text. Call DURING render pass, after upload_queued_text.
     */
    void draw_queued_text(SDL_GPUCommandBuffer* cmd, SDL_GPURenderPass* render_pass);

    /**
     * Draw text immediately without uploading vertex data.
     * Used during execute phase when vertex buffer is already uploaded.
     * Uses push uniforms for quad transform instead of vertex buffer updates.
     */
    void draw_text_immediate(SDL_GPUCommandBuffer* cmd, SDL_GPURenderPass* render_pass,
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

    /**
     * Prepare text textures for rendering. Call BEFORE starting a render pass.
     * This creates and uploads any textures needed for the given text strings.
     * @param cmd Command buffer for copy operations
     * @param texts Vector of text strings that will be drawn this frame
     */
    void prepare_text_textures(SDL_GPUCommandBuffer* cmd, const std::vector<std::string>& texts);

    /**
     * Create textures for any text that was requested but not cached.
     * Call AFTER render passes end (e.g., at end of frame).
     * @param cmd Command buffer for copy operations
     */
    void create_pending_textures(SDL_GPUCommandBuffer* cmd);

    /**
     * Get or create a cached texture for the given text.
     * If called during a render pass and texture doesn't exist, returns nullptr.
     * @param cmd Command buffer (for upload if not in render pass)
     * @param text The text string
     * @param in_render_pass Whether we're currently in a render pass
     * @return Texture pointer and dimensions, or nullptr if unavailable
     */
    struct CachedText {
        SDL_GPUTexture* texture = nullptr;
        int width = 0;
        int height = 0;
        uint64_t last_used_frame = 0;
    };
    CachedText* get_or_create_text_texture(SDL_GPUCommandBuffer* cmd, const std::string& text, bool in_render_pass);

private:
    TTF_Font* font_ = nullptr;
    int font_size_ = 18;
    bool initialized_ = false;
    
    gpu::GPUDevice* device_ = nullptr;
    gpu::PipelineRegistry* pipeline_registry_ = nullptr;
    glm::mat4 projection_;
    
    // Dynamic vertex buffer for text quads (legacy, used by draw_text)
    std::unique_ptr<gpu::GPUBuffer> vertex_buffer_;

    // Static unit quad vertex buffer for draw_text_immediate
    // Contains a 0-1 unit quad that gets transformed via push uniforms
    std::unique_ptr<gpu::GPUBuffer> unit_quad_buffer_;

    // Sampler for text textures
    SDL_GPUSampler* sampler_ = nullptr;
    
    // Pending GPU resources to be released after GPU has finished using them
    // We use double-buffering: resources go to pending_*, then released next frame
    std::vector<SDL_GPUTexture*> pending_textures_;
    std::vector<SDL_GPUTransferBuffer*> pending_transfers_;

    // Text texture cache - maps text string to cached texture info
    std::unordered_map<std::string, CachedText> text_cache_;
    uint64_t current_frame_ = 0;
    static constexpr uint64_t CACHE_EXPIRY_FRAMES = 300;  // Remove unused textures after ~5 seconds at 60fps

    // Texts that were requested but not in cache - created at end of frame
    std::vector<std::string> pending_text_creates_;

    // Batched text rendering data
    struct QueuedText {
        std::string text;
        float x, y;
        uint32_t color;
        float scale;
        size_t vertex_offset;  // Offset into batch vertex buffer
    };
    std::vector<QueuedText> queued_texts_;
    std::vector<float> batch_vertices_;  // All vertex data for queued texts
    static constexpr size_t VERTICES_PER_QUAD = 6;
    static constexpr size_t FLOATS_PER_VERTEX = 4;  // x, y, u, v
    static constexpr size_t MAX_QUEUED_TEXTS = 256;
};

} // namespace mmo::engine::render
