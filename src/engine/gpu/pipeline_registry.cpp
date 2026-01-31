#include "pipeline_registry.hpp"
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_filesystem.h>

namespace mmo::gpu {

// =============================================================================
// PipelineRegistry Implementation
// =============================================================================

PipelineRegistry::~PipelineRegistry() {
    shutdown();

}

bool PipelineRegistry::init(GPUDevice& device) {
    if (device_) {
        SDL_Log("PipelineRegistry: Already initialized");
        return false;
    }

    device_ = &device;

    // Get swapchain format from device
    swapchain_format_ = device_->swapchain_format();

    // Set shader path relative to executable location
    const char* base_path = SDL_GetBasePath();
    if (base_path) {
        shader_path_ = std::string(base_path) + "shaders/";
    } else {
        shader_path_ = "shaders/";
        SDL_Log("PipelineRegistry: SDL_GetBasePath failed, using relative path");
    }

    // Create shader manager (shaders are pre-compiled to SPIRV at build time)
    shader_manager_ = std::make_unique<ShaderManager>(device);

    SDL_Log("PipelineRegistry: Initialized with shader path: %s", shader_path_.c_str());

    return true;
}

void PipelineRegistry::shutdown() {
    if (!device_ && pipelines_.empty() && !shader_manager_) {
        return;  // Already shut down
    }
    pipelines_.clear();
    shader_manager_.reset();
    device_ = nullptr;
    SDL_Log("PipelineRegistry: Shutdown complete");
}

GPUPipeline* PipelineRegistry::get_pipeline(PipelineType type) {
    if (!device_) {
        SDL_Log("PipelineRegistry: Not initialized");
        return nullptr;
    }

    // Check cache first
    auto it = pipelines_.find(type);
    if (it != pipelines_.end()) {
        return it->second.get();
    }

    // Create pipeline on demand
    std::unique_ptr<GPUPipeline> pipeline;

    switch (type) {
        case PipelineType::Model:
            pipeline = create_model_pipeline();
            break;
        case PipelineType::SkinnedModel:
            pipeline = create_skinned_model_pipeline();
            break;
        case PipelineType::Terrain:
            pipeline = create_terrain_pipeline();
            break;
        case PipelineType::Skybox:
            pipeline = create_skybox_pipeline();
            break;
        case PipelineType::Grid:
            pipeline = create_grid_pipeline();
            break;
        case PipelineType::UI:
            pipeline = create_ui_pipeline();
            break;
        case PipelineType::Text:
            pipeline = create_text_pipeline();
            break;
        case PipelineType::Billboard:
            pipeline = create_billboard_pipeline();
            break;
        case PipelineType::Effect:
            pipeline = create_effect_pipeline();
            break;
        case PipelineType::Grass:
            pipeline = create_grass_pipeline();
            break;
        default:
            SDL_Log("PipelineRegistry: Unknown pipeline type %d", static_cast<int>(type));
            return nullptr;
    }

    if (!pipeline) {
        SDL_Log("PipelineRegistry: Failed to create %s pipeline",
                pipeline_type_to_string(type));
        return nullptr;
    }

    SDL_Log("PipelineRegistry: Created %s pipeline", pipeline_type_to_string(type));

    auto* result = pipeline.get();
    pipelines_[type] = std::move(pipeline);
    return result;
}

bool PipelineRegistry::preload_all_pipelines() {
    SDL_Log("PipelineRegistry: Preloading all pipelines...");

    bool success = true;
    for (int i = 0; i < static_cast<int>(PipelineType::Count); ++i) {
        auto type = static_cast<PipelineType>(i);
        if (!get_pipeline(type)) {
            SDL_Log("PipelineRegistry: Failed to preload %s pipeline",
                    pipeline_type_to_string(type));
            success = false;
        }
    }

    SDL_Log("PipelineRegistry: Preloaded %zu pipelines", pipelines_.size());
    return success;
}

void PipelineRegistry::invalidate_all() {
    pipelines_.clear();
    if (shader_manager_) {
        shader_manager_->clear_cache();
    }
    SDL_Log("PipelineRegistry: All pipelines invalidated");
}

void PipelineRegistry::set_swapchain_format(SDL_GPUTextureFormat format) {
    if (format != swapchain_format_) {
        swapchain_format_ = format;
        invalidate_all();
    }
}

// =============================================================================
// Pipeline Creation Methods
// =============================================================================

std::unique_ptr<GPUPipeline> PipelineRegistry::create_model_pipeline() {
    ShaderResources vs_resources;
    vs_resources.num_uniform_buffers = 1;

    ShaderResources fs_resources;
    fs_resources.num_uniform_buffers = 1;
    fs_resources.num_samplers = 1;  // baseColor

    auto* vs = shader_manager_->get(shader_path_ + "model.vert.spv",
                                     ShaderStage::Vertex, "VSMain", vs_resources);
    auto* fs = shader_manager_->get(shader_path_ + "model.frag.spv",
                                     ShaderStage::Fragment, "PSMain", fs_resources);

    if (!vs || !fs) return nullptr;

    PipelineConfig config;
    config.vertex_shader = vs->handle();
    config.fragment_shader = fs->handle();
    config.with_vertex3d().opaque();
    config.color_format = swapchain_format_;
    config.depth_format = depth_format_;

    return GPUPipeline::create(*device_, config);
}

std::unique_ptr<GPUPipeline> PipelineRegistry::create_skinned_model_pipeline() {
    ShaderResources vs_resources;
    vs_resources.num_uniform_buffers = 2; // Camera + Bones

    ShaderResources fs_resources;
    fs_resources.num_uniform_buffers = 1;
    fs_resources.num_samplers = 1;  // baseColor

    auto* vs = shader_manager_->get(shader_path_ + "skinned_model.vert.spv",
                                     ShaderStage::Vertex, "VSMain", vs_resources);
    auto* fs = shader_manager_->get(shader_path_ + "skinned_model.frag.spv",
                                     ShaderStage::Fragment, "PSMain", fs_resources);

    if (!vs || !fs) return nullptr;

    PipelineConfig config;
    config.vertex_shader = vs->handle();
    config.fragment_shader = fs->handle();
    config.with_skinned_vertex().opaque();
    config.color_format = swapchain_format_;
    config.depth_format = depth_format_;

    return GPUPipeline::create(*device_, config);
}

std::unique_ptr<GPUPipeline> PipelineRegistry::create_terrain_pipeline() {
    ShaderResources vs_resources;
    vs_resources.num_uniform_buffers = 1;

    ShaderResources fs_resources;
    fs_resources.num_uniform_buffers = 1;
    fs_resources.num_samplers = 1;  // grassTexture

    auto* vs = shader_manager_->get(shader_path_ + "terrain.vert.spv",
                                     ShaderStage::Vertex, "VSMain", vs_resources);
    auto* fs = shader_manager_->get(shader_path_ + "terrain.frag.spv",
                                     ShaderStage::Fragment, "PSMain", fs_resources);

    if (!vs || !fs) return nullptr;

    // Custom terrain vertex format: position(3), texcoord(2), color(4)
    PipelineConfig config;
    config.vertex_shader = vs->handle();
    config.fragment_shader = fs->handle();

    config.vertex_buffers = {{
        .slot = 0,
        .pitch = sizeof(float) * 9,  // 3 + 2 + 4 floats
        .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX,
        .instance_step_rate = 0
    }};

    config.vertex_attributes = {
        { 0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 0 },                    // position
        { 1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, sizeof(float) * 3 },    // texcoord
        { 2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, sizeof(float) * 5 },    // color
    };

    config.opaque();
    config.color_format = swapchain_format_;
    config.depth_format = depth_format_;

    return GPUPipeline::create(*device_, config);
}

std::unique_ptr<GPUPipeline> PipelineRegistry::create_skybox_pipeline() {
    ShaderResources vs_resources;
    // Skybox vertex shader has no uniforms (fullscreen triangle, no transforms)

    ShaderResources fs_resources;
    fs_resources.num_uniform_buffers = 1;

    auto* vs = shader_manager_->get(shader_path_ + "skybox.vert.spv",
                                     ShaderStage::Vertex, "VSMain", vs_resources);
    auto* fs = shader_manager_->get(shader_path_ + "skybox.frag.spv",
                                     ShaderStage::Fragment, "PSMain", fs_resources);

    if (!vs || !fs) return nullptr;

    PipelineConfig config;
    config.vertex_shader = vs->handle();
    config.fragment_shader = fs->handle();

    // Skybox uses position-only vertices (float3), not full Vertex3D
    config.vertex_buffers = {{
        .slot = 0,
        .pitch = sizeof(float) * 3,
        .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX,
        .instance_step_rate = 0
    }};
    config.vertex_attributes = {
        { 0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 0 },  // position only
    };

    config.opaque().no_cull();
    config.depth_write_enable = false;
    config.depth_compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
    config.color_format = swapchain_format_;
    config.depth_format = depth_format_;

    return GPUPipeline::create(*device_, config);
}

std::unique_ptr<GPUPipeline> PipelineRegistry::create_grid_pipeline() {
    ShaderResources vs_resources;
    vs_resources.num_uniform_buffers = 1;

    ShaderResources fs_resources;
    // No resources needed

    auto* vs = shader_manager_->get(shader_path_ + "grid.vert.spv",
                                     ShaderStage::Vertex, "VSMain", vs_resources);
    auto* fs = shader_manager_->get(shader_path_ + "grid.frag.spv",
                                     ShaderStage::Fragment, "PSMain", fs_resources);

    if (!vs || !fs) return nullptr;

    PipelineConfig config;
    config.vertex_shader = vs->handle();
    config.fragment_shader = fs->handle();
    config.with_vertex3d().alpha_blended().no_cull();
    config.color_format = swapchain_format_;
    config.depth_format = depth_format_;
    config.primitive_type = SDL_GPU_PRIMITIVETYPE_LINELIST;

    return GPUPipeline::create(*device_, config);
}

std::unique_ptr<GPUPipeline> PipelineRegistry::create_ui_pipeline() {
    ShaderResources vs_resources;
    vs_resources.num_uniform_buffers = 1;  // screen size uniform

    ShaderResources fs_resources;
    fs_resources.num_uniform_buffers = 1;  // has_texture flag
    fs_resources.num_samplers = 1;  // texture sampler (even if not always used)

    SDL_Log("PipelineRegistry: Creating UI shaders with vs_uniforms=%d, fs_uniforms=%d, fs_samplers=%d",
            vs_resources.num_uniform_buffers, fs_resources.num_uniform_buffers, fs_resources.num_samplers);

    auto* vs = shader_manager_->get(shader_path_ + "ui.vert.spv",
                                     ShaderStage::Vertex, "VSMain", vs_resources);
    auto* fs = shader_manager_->get(shader_path_ + "ui.frag.spv",
                                     ShaderStage::Fragment, "PSMain", fs_resources);

    if (!vs || !fs) return nullptr;

    PipelineConfig config;
    config.vertex_shader = vs->handle();
    config.fragment_shader = fs->handle();
    config.with_vertex2d().alpha_blended().no_depth().no_cull();
    config.color_format = swapchain_format_;

    return GPUPipeline::create(*device_, config);
}

std::unique_ptr<GPUPipeline> PipelineRegistry::create_text_pipeline() {
    ShaderResources vs_resources;
    vs_resources.num_uniform_buffers = 1;  // projection

    ShaderResources fs_resources;
    fs_resources.num_uniform_buffers = 1;  // text color
    fs_resources.num_samplers = 1;         // font texture

    auto* vs = shader_manager_->get(shader_path_ + "text.vert.spv",
                                     ShaderStage::Vertex, "VSMain", vs_resources);
    auto* fs = shader_manager_->get(shader_path_ + "text.frag.spv",
                                     ShaderStage::Fragment, "PSMain", fs_resources);

    if (!vs || !fs) return nullptr;

    // Text uses 4-float vertex format: position(2), texcoord(2)
    // Color is passed as a uniform, not per-vertex
    PipelineConfig config;
    config.vertex_shader = vs->handle();
    config.fragment_shader = fs->handle();

    config.vertex_buffers = {{
        .slot = 0,
        .pitch = sizeof(float) * 4,  // 2 + 2 floats
        .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX,
        .instance_step_rate = 0
    }};

    config.vertex_attributes = {
        { 0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, 0 },                    // position
        { 1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, sizeof(float) * 2 },    // texcoord
    };

    config.alpha_blended().no_depth().no_cull();
    config.color_format = swapchain_format_;

    return GPUPipeline::create(*device_, config);
}

std::unique_ptr<GPUPipeline> PipelineRegistry::create_billboard_pipeline() {
    ShaderResources vs_resources;
    vs_resources.num_uniform_buffers = 1;

    ShaderResources fs_resources;
    fs_resources.num_samplers = 1;

    auto* vs = shader_manager_->get(shader_path_ + "billboard.vert.spv",
                                     ShaderStage::Vertex, "VSMain", vs_resources);
    auto* fs = shader_manager_->get(shader_path_ + "billboard.frag.spv",
                                     ShaderStage::Fragment, "PSMain", fs_resources);

    if (!vs || !fs) return nullptr;

    PipelineConfig config;
    config.vertex_shader = vs->handle();
    config.fragment_shader = fs->handle();
    config.with_vertex3d().alpha_blended().no_cull();
    config.color_format = swapchain_format_;
    config.depth_format = depth_format_;

    return GPUPipeline::create(*device_, config);
}

std::unique_ptr<GPUPipeline> PipelineRegistry::create_effect_pipeline() {
    ShaderResources vs_resources;
    vs_resources.num_uniform_buffers = 1;

    ShaderResources fs_resources;
    fs_resources.num_samplers = 1;

    auto* vs = shader_manager_->get(shader_path_ + "effect.vert.spv",
                                     ShaderStage::Vertex, "VSMain", vs_resources);
    auto* fs = shader_manager_->get(shader_path_ + "effect.frag.spv",
                                     ShaderStage::Fragment, "PSMain", fs_resources);

    if (!vs || !fs) return nullptr;

    PipelineConfig config;
    config.vertex_shader = vs->handle();
    config.fragment_shader = fs->handle();
    config.with_vertex3d().additive().no_cull();
    config.depth_write_enable = false;
    config.color_format = swapchain_format_;
    config.depth_format = depth_format_;

    return GPUPipeline::create(*device_, config);
}

std::unique_ptr<GPUPipeline> PipelineRegistry::create_grass_pipeline() {
    ShaderResources vs_resources;
    vs_resources.num_uniform_buffers = 1;

    ShaderResources fs_resources;
    fs_resources.num_uniform_buffers = 1;
    fs_resources.num_samplers = 1;

    auto* vs = shader_manager_->get(shader_path_ + "grass.vert.spv",
                                     ShaderStage::Vertex, "VSMain", vs_resources);
    auto* fs = shader_manager_->get(shader_path_ + "grass.frag.spv",
                                     ShaderStage::Fragment, "PSMain", fs_resources);

    if (!vs || !fs) return nullptr;

    PipelineConfig config;
    config.vertex_shader = vs->handle();
    config.fragment_shader = fs->handle();
    config.with_vertex3d().opaque().no_cull();
    config.color_format = swapchain_format_;
    config.depth_format = depth_format_;

    return GPUPipeline::create(*device_, config);
}

} // namespace mmo::gpu
