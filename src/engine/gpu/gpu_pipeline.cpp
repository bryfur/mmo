#include "gpu_pipeline.hpp"
#include "SDL3/SDL_error.h"
#include "SDL3/SDL_gpu.h"
#include "SDL3/SDL_stdinc.h"
#include "engine/gpu/gpu_device.hpp"
#include "engine/gpu/gpu_types.hpp"
#include <SDL3/SDL_log.h>
#include <memory>

namespace mmo::engine::gpu {

// =============================================================================
// PipelineConfig Implementation
// =============================================================================

PipelineConfig& PipelineConfig::with_vertex3d() {
    vertex_buffers = { get_vertex3d_buffer_desc() };
    vertex_attributes = get_vertex3d_attributes();
    return *this;
}

PipelineConfig& PipelineConfig::with_skinned_vertex() {
    vertex_buffers = { get_skinned_vertex_buffer_desc() };
    vertex_attributes = get_skinned_vertex_attributes();
    return *this;
}

PipelineConfig& PipelineConfig::with_vertex2d() {
    vertex_buffers = { get_vertex2d_buffer_desc() };
    vertex_attributes = get_vertex2d_attributes();
    return *this;
}

PipelineConfig& PipelineConfig::opaque() {
    blend_mode = BlendMode::None;
    depth_test_enable = true;
    depth_write_enable = true;
    return *this;
}

PipelineConfig& PipelineConfig::alpha_blended() {
    blend_mode = BlendMode::Alpha;
    depth_test_enable = true;
    depth_write_enable = false; // Usually don't write depth for transparent objects
    return *this;
}

PipelineConfig& PipelineConfig::additive() {
    blend_mode = BlendMode::Additive;
    depth_test_enable = true;
    depth_write_enable = false;
    return *this;
}

PipelineConfig& PipelineConfig::no_depth() {
    depth_test_enable = false;
    depth_write_enable = false;
    has_depth_target = false;
    return *this;
}

PipelineConfig& PipelineConfig::no_cull() {
    cull_mode = SDL_GPU_CULLMODE_NONE;
    return *this;
}

PipelineConfig& PipelineConfig::cull_front() {
    cull_mode = SDL_GPU_CULLMODE_FRONT;
    return *this;
}

PipelineConfig& PipelineConfig::with_depth_bias(float constant, float slope, float clamp) {
    enable_depth_bias = true;
    depth_bias_constant = constant;
    depth_bias_slope = slope;
    depth_bias_clamp = clamp;
    return *this;
}

// =============================================================================
// GPUPipeline Implementation
// =============================================================================

GPUPipeline::~GPUPipeline() {
    if (device_ && pipeline_) {
        device_->release_graphics_pipeline(pipeline_);
    }
}

GPUPipeline::GPUPipeline(GPUPipeline&& other) noexcept
    : device_(other.device_)
    , pipeline_(other.pipeline_) {
    other.device_ = nullptr;
    other.pipeline_ = nullptr;
}

GPUPipeline& GPUPipeline::operator=(GPUPipeline&& other) noexcept {
    if (this != &other) {
        if (device_ && pipeline_) {
            device_->release_graphics_pipeline(pipeline_);
        }

        device_ = other.device_;
        pipeline_ = other.pipeline_;

        other.device_ = nullptr;
        other.pipeline_ = nullptr;
    }
    return *this;
}

std::unique_ptr<GPUPipeline> GPUPipeline::create(GPUDevice& device, const PipelineConfig& config) {
    if (!config.vertex_shader || !config.fragment_shader) {
        SDL_Log("GPUPipeline::create: Missing shaders");
        return nullptr;
    }

    auto pipeline = std::unique_ptr<GPUPipeline>(new GPUPipeline());
    pipeline->device_ = &device;

    // Build the pipeline create info
    SDL_GPUGraphicsPipelineCreateInfo pipeline_info = {};

    // Shaders
    pipeline_info.vertex_shader = config.vertex_shader;
    pipeline_info.fragment_shader = config.fragment_shader;

    // Vertex input state
    SDL_GPUVertexInputState vertex_input = {};
    vertex_input.vertex_buffer_descriptions = config.vertex_buffers.data();
    vertex_input.num_vertex_buffers = static_cast<Uint32>(config.vertex_buffers.size());
    vertex_input.vertex_attributes = config.vertex_attributes.data();
    vertex_input.num_vertex_attributes = static_cast<Uint32>(config.vertex_attributes.size());
    pipeline_info.vertex_input_state = vertex_input;

    // Primitive type
    pipeline_info.primitive_type = config.primitive_type;

    // Rasterizer state
    SDL_GPURasterizerState rasterizer = {};
    rasterizer.fill_mode = config.fill_mode;
    rasterizer.cull_mode = config.cull_mode;
    rasterizer.front_face = config.front_face;
    rasterizer.depth_bias_constant_factor = config.depth_bias_constant;
    rasterizer.depth_bias_clamp = config.depth_bias_clamp;
    rasterizer.depth_bias_slope_factor = config.depth_bias_slope;
    rasterizer.enable_depth_bias = config.enable_depth_bias;
    rasterizer.enable_depth_clip = config.enable_depth_clip;
    pipeline_info.rasterizer_state = rasterizer;

    // Multisample state
    SDL_GPUMultisampleState multisample = {};
    multisample.sample_count = config.sample_count;
    multisample.sample_mask = 0;  // Must be 0 for SDL3 GPU API
    pipeline_info.multisample_state = multisample;

    // Depth/stencil state
    SDL_GPUDepthStencilState depth_stencil = {};
    depth_stencil.enable_depth_test = config.depth_test_enable;
    depth_stencil.enable_depth_write = config.depth_write_enable;
    depth_stencil.compare_op = config.depth_compare_op;
    depth_stencil.enable_stencil_test = config.stencil_test_enable;
    pipeline_info.depth_stencil_state = depth_stencil;

    // Color target (single target for most pipelines, 0 for depth-only)
    SDL_GPUColorTargetDescription color_target = {};
    if (config.color_format != SDL_GPU_TEXTUREFORMAT_INVALID) {
        color_target.format = config.color_format;
        color_target.blend_state = get_blend_state(config.blend_mode);
        pipeline_info.target_info.color_target_descriptions = &color_target;
        pipeline_info.target_info.num_color_targets = 1;
    } else {
        // Depth-only pipeline
        pipeline_info.target_info.color_target_descriptions = nullptr;
        pipeline_info.target_info.num_color_targets = 0;
    }

    // Depth target
    if (config.has_depth_target) {
        pipeline_info.target_info.depth_stencil_format = config.depth_format;
        pipeline_info.target_info.has_depth_stencil_target = true;
    } else {
        pipeline_info.target_info.has_depth_stencil_target = false;
    }

    // Create the pipeline
    pipeline->pipeline_ = device.create_graphics_pipeline(pipeline_info);
    if (!pipeline->pipeline_) {
        SDL_Log("GPUPipeline::create: Failed to create pipeline: %s", SDL_GetError());
        return nullptr;
    }

    return pipeline;
}

void GPUPipeline::bind(SDL_GPURenderPass* render_pass) {
    if (!render_pass || !pipeline_) {
        return;
    }
    SDL_BindGPUGraphicsPipeline(render_pass, pipeline_);
}

} // namespace mmo::engine::gpu
