#pragma once

#include "gpu_device.hpp"
#include "gpu_types.hpp"
#include <memory>
#include <vector>

namespace mmo::gpu {

// Forward declarations
class GPUShader;

/**
 * @brief Configuration for creating a graphics pipeline
 * 
 * This structure contains all the state needed to create a complete graphics
 * pipeline. Use the helper methods to set up common configurations.
 */
struct PipelineConfig {
    // Shaders (required)
    SDL_GPUShader* vertex_shader = nullptr;
    SDL_GPUShader* fragment_shader = nullptr;

    // Vertex input
    std::vector<SDL_GPUVertexBufferDescription> vertex_buffers;
    std::vector<SDL_GPUVertexAttribute> vertex_attributes;

    // Primitive assembly
    SDL_GPUPrimitiveType primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;

    // Rasterizer state
    SDL_GPUFillMode fill_mode = SDL_GPU_FILLMODE_FILL;
    SDL_GPUCullMode cull_mode = SDL_GPU_CULLMODE_BACK;
    SDL_GPUFrontFace front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    float depth_bias_constant = 0.0f;
    float depth_bias_clamp = 0.0f;
    float depth_bias_slope = 0.0f;
    bool enable_depth_bias = false;
    bool enable_depth_clip = true;

    // Depth/stencil state
    bool depth_test_enable = true;
    bool depth_write_enable = true;
    SDL_GPUCompareOp depth_compare_op = SDL_GPU_COMPAREOP_LESS;
    bool stencil_test_enable = false;

    // Color targets
    BlendMode blend_mode = BlendMode::None;
    SDL_GPUTextureFormat color_format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    
    // Depth format (set to invalid to disable depth)
    bool has_depth_target = true;
    SDL_GPUTextureFormat depth_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;

    // Multi-sample state
    SDL_GPUSampleCount sample_count = SDL_GPU_SAMPLECOUNT_1;

    // Helper methods for common setups
    
    /// Set up for Vertex3D format
    PipelineConfig& with_vertex3d();
    
    /// Set up for SkinnedVertex format
    PipelineConfig& with_skinned_vertex();
    
    /// Set up for Vertex2D format (UI)
    PipelineConfig& with_vertex2d();

    /// Configure for opaque rendering
    PipelineConfig& opaque();

    /// Configure for alpha blended rendering
    PipelineConfig& alpha_blended();

    /// Configure for additive blending
    PipelineConfig& additive();

    /// Disable depth test/write (for UI, skybox, etc.)
    PipelineConfig& no_depth();

    /// Disable culling (for double-sided geometry)
    PipelineConfig& no_cull();

    /// Use front-face culling (for skybox from inside)
    PipelineConfig& cull_front();

    /// Set depth bias for shadow mapping
    PipelineConfig& with_depth_bias(float constant, float slope, float clamp = 0.0f);
};

/**
 * @brief GPU Graphics Pipeline wrapper
 * 
 * This class wraps an SDL_GPUGraphicsPipeline and provides a clean interface
 * for binding during rendering.
 * 
 * Usage:
 *   PipelineConfig config;
 *   config.vertex_shader = vs;
 *   config.fragment_shader = fs;
 *   config.with_vertex3d().opaque();
 *   config.color_format = device.swapchain_format();
 *   
 *   auto pipeline = GPUPipeline::create(device, config);
 *   
 *   // In render loop:
 *   pipeline->bind(render_pass);
 */
class GPUPipeline {
public:
    ~GPUPipeline();

    // Non-copyable, movable
    GPUPipeline(const GPUPipeline&) = delete;
    GPUPipeline& operator=(const GPUPipeline&) = delete;
    GPUPipeline(GPUPipeline&& other) noexcept;
    GPUPipeline& operator=(GPUPipeline&& other) noexcept;

    /**
     * @brief Create a graphics pipeline from configuration
     * 
     * @param device The GPU device
     * @param config Pipeline configuration
     * @return Unique pointer to the pipeline, or nullptr on failure
     */
    static std::unique_ptr<GPUPipeline> create(GPUDevice& device, const PipelineConfig& config);

    /**
     * @brief Bind this pipeline for use in a render pass
     * 
     * @param render_pass The current render pass
     */
    void bind(SDL_GPURenderPass* render_pass);

    /**
     * @brief Get the raw SDL GPU pipeline handle
     */
    SDL_GPUGraphicsPipeline* handle() const { return pipeline_; }

private:
    GPUPipeline() = default;

    GPUDevice* device_ = nullptr;
    SDL_GPUGraphicsPipeline* pipeline_ = nullptr;
};

} // namespace mmo::gpu
