#include "pipeline_registry.hpp"
#include <SDL3/SDL_log.h>
#include <cassert>

namespace mmo::gpu {

// =============================================================================
// HLSL Shader Sources
// =============================================================================
// These are temporary embedded shaders for initial implementation.
// In a full implementation, these would be loaded from files in shaders/src/

namespace hlsl {

// Model vertex shader
const char* const model_vertex = R"(
struct VSInput {
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 texcoord : TEXCOORD0;
    float4 color : COLOR0;
};

struct VSOutput {
    float4 position : SV_Position;
    float3 frag_pos : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float2 texcoord : TEXCOORD2;
    float4 color : TEXCOORD3;
    float fog_distance : TEXCOORD4;
    float4 light_space_pos : TEXCOORD5;
};

cbuffer Uniforms : register(b0) {
    float4x4 model;
    float4x4 view;
    float4x4 projection;
    float3 camera_pos;
    float _padding;
    float4x4 light_space_matrix;
};

VSOutput VSMain(VSInput input) {
    VSOutput output;
    
    float4 world_pos = mul(model, float4(input.position, 1.0));
    output.frag_pos = world_pos.xyz;
    output.normal = mul((float3x3)model, input.normal);
    output.texcoord = input.texcoord;
    output.color = input.color;
    output.fog_distance = length(world_pos.xyz - camera_pos);
    output.light_space_pos = mul(light_space_matrix, world_pos);
    output.position = mul(projection, mul(view, world_pos));
    
    return output;
}
)";

// Model fragment shader
const char* const model_fragment = R"(
struct PSInput {
    float4 position : SV_Position;
    float3 frag_pos : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float2 texcoord : TEXCOORD2;
    float4 color : TEXCOORD3;
    float fog_distance : TEXCOORD4;
    float4 light_space_pos : TEXCOORD5;
};

cbuffer Uniforms : register(b0) {
    float3 light_dir;
    float ambient;
    float3 light_color;
    float _padding1;
    float4 tint_color;
    float3 fog_color;
    float fog_start;
    float fog_end;
    int has_texture;
    int shadows_enabled;
    int fog_enabled;
};

Texture2D base_color_texture : register(t0);
SamplerState base_sampler : register(s0);

Texture2D shadow_map : register(t1);
SamplerComparisonState shadow_sampler : register(s1);

float4 PSMain(PSInput input) : SV_Target {
    float3 normal = normalize(input.normal);
    float3 light_direction = normalize(-light_dir);
    
    // Diffuse lighting
    float diff = max(dot(normal, light_direction), 0.0);
    float3 diffuse = diff * light_color;
    
    // Get base color
    float4 base_color;
    if (has_texture == 1) {
        base_color = base_color_texture.Sample(base_sampler, input.texcoord);
    } else {
        base_color = input.color * tint_color;
    }
    
    // Combine lighting
    float3 ambient_color = float3(ambient, ambient, ambient);
    float3 result = (ambient_color + diffuse) * base_color.rgb;
    
    // Apply fog
    if (fog_enabled == 1) {
        float fog_factor = saturate((input.fog_distance - fog_start) / (fog_end - fog_start));
        fog_factor = 1.0 - exp(-fog_factor * 2.0);
        result = lerp(result, fog_color, fog_factor);
    }
    
    return float4(result, base_color.a);
}
)";

// UI vertex shader
const char* const ui_vertex = R"(
struct VSInput {
    float2 position : POSITION;
    float2 texcoord : TEXCOORD0;
    float4 color : COLOR0;
};

struct VSOutput {
    float4 position : SV_Position;
    float2 texcoord : TEXCOORD0;
    float4 color : TEXCOORD1;
};

cbuffer Uniforms : register(b0) {
    float4x4 projection;
};

VSOutput VSMain(VSInput input) {
    VSOutput output;
    output.position = mul(projection, float4(input.position, 0.0, 1.0));
    output.texcoord = input.texcoord;
    output.color = input.color;
    return output;
}
)";

// UI fragment shader
const char* const ui_fragment = R"(
struct PSInput {
    float4 position : SV_Position;
    float2 texcoord : TEXCOORD0;
    float4 color : TEXCOORD1;
};

cbuffer Uniforms : register(b0) {
    int has_texture;
    int _padding[3];
};

Texture2D ui_texture : register(t0);
SamplerState ui_sampler : register(s0);

float4 PSMain(PSInput input) : SV_Target {
    float4 color = input.color;
    if (has_texture == 1) {
        color *= ui_texture.Sample(ui_sampler, input.texcoord);
    }
    return color;
}
)";

// Skybox vertex shader
const char* const skybox_vertex = R"(
struct VSInput {
    float3 position : POSITION;
};

struct VSOutput {
    float4 position : SV_Position;
    float3 texcoord : TEXCOORD0;
};

cbuffer Uniforms : register(b0) {
    float4x4 view_projection;
};

VSOutput VSMain(VSInput input) {
    VSOutput output;
    output.texcoord = input.position;
    output.position = mul(view_projection, float4(input.position, 1.0));
    // Set z = w so depth is always 1.0 (far plane)
    output.position.z = output.position.w;
    return output;
}
)";

// Skybox fragment shader
const char* const skybox_fragment = R"(
struct PSInput {
    float4 position : SV_Position;
    float3 texcoord : TEXCOORD0;
};

cbuffer Uniforms : register(b0) {
    float3 sky_color_top;
    float _padding1;
    float3 sky_color_bottom;
    float _padding2;
};

float4 PSMain(PSInput input) : SV_Target {
    float3 dir = normalize(input.texcoord);
    float t = dir.y * 0.5 + 0.5;
    float3 color = lerp(sky_color_bottom, sky_color_top, t);
    return float4(color, 1.0);
}
)";

// Terrain vertex shader
const char* const terrain_vertex = R"(
struct VSInput {
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 texcoord : TEXCOORD0;
    float4 color : COLOR0;
};

struct VSOutput {
    float4 position : SV_Position;
    float3 frag_pos : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float2 texcoord : TEXCOORD2;
    float4 color : TEXCOORD3;
    float4 light_space_pos : TEXCOORD4;
};

cbuffer Uniforms : register(b0) {
    float4x4 model;
    float4x4 view;
    float4x4 projection;
    float4x4 light_space_matrix;
};

VSOutput VSMain(VSInput input) {
    VSOutput output;
    
    float4 world_pos = mul(model, float4(input.position, 1.0));
    output.frag_pos = world_pos.xyz;
    output.normal = mul((float3x3)model, input.normal);
    output.texcoord = input.texcoord;
    output.color = input.color;
    output.light_space_pos = mul(light_space_matrix, world_pos);
    output.position = mul(projection, mul(view, world_pos));
    
    return output;
}
)";

// Terrain fragment shader
const char* const terrain_fragment = R"(
struct PSInput {
    float4 position : SV_Position;
    float3 frag_pos : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float2 texcoord : TEXCOORD2;
    float4 color : TEXCOORD3;
    float4 light_space_pos : TEXCOORD4;
};

cbuffer Uniforms : register(b0) {
    float3 light_dir;
    float ambient;
    float3 light_color;
    float texture_scale;
};

Texture2D grass_texture : register(t0);
Texture2D rock_texture : register(t1);
Texture2D splatmap : register(t2);
SamplerState terrain_sampler : register(s0);

float4 PSMain(PSInput input) : SV_Target {
    float3 normal = normalize(input.normal);
    float3 light_direction = normalize(-light_dir);
    
    // Sample terrain textures
    float2 scaled_uv = input.texcoord * texture_scale;
    float4 grass_color = grass_texture.Sample(terrain_sampler, scaled_uv);
    float4 rock_color = rock_texture.Sample(terrain_sampler, scaled_uv);
    
    // Blend based on splatmap (or slope)
    float slope = 1.0 - normal.y;
    float rock_blend = saturate(slope * 3.0);
    float4 base_color = lerp(grass_color, rock_color, rock_blend);
    
    // Lighting
    float diff = max(dot(normal, light_direction), 0.0);
    float3 ambient_color = float3(ambient, ambient, ambient);
    float3 result = (ambient_color + diff * light_color) * base_color.rgb;
    
    return float4(result, 1.0);
}
)";

// Shadow depth vertex shader (static models)
const char* const shadow_vertex = R"(
struct VSInput {
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 texcoord : TEXCOORD0;
    float4 color : COLOR0;
};

struct VSOutput {
    float4 position : SV_Position;
};

cbuffer Uniforms : register(b0) {
    float4x4 light_space_matrix;
    float4x4 model;
};

VSOutput VSMain(VSInput input) {
    VSOutput output;
    float4 world_pos = mul(model, float4(input.position, 1.0));
    output.position = mul(light_space_matrix, world_pos);
    return output;
}
)";

// Shadow depth fragment shader
const char* const shadow_fragment = R"(
struct PSInput {
    float4 position : SV_Position;
};

void PSMain(PSInput input) {
    // Depth is written automatically
}
)";

// Billboard vertex shader
const char* const billboard_vertex = R"(
struct VSInput {
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 texcoord : TEXCOORD0;
    float4 color : COLOR0;
};

struct VSOutput {
    float4 position : SV_Position;
    float2 texcoord : TEXCOORD0;
    float4 color : TEXCOORD1;
};

cbuffer Uniforms : register(b0) {
    float4x4 model;
    float4x4 view;
    float4x4 projection;
    float3 camera_right;
    float _padding1;
    float3 camera_up;
    float _padding2;
};

VSOutput VSMain(VSInput input) {
    VSOutput output;
    
    // Billboard - always face camera
    float3 world_pos = mul(model, float4(0, 0, 0, 1)).xyz;
    world_pos += camera_right * input.position.x + camera_up * input.position.y;
    
    output.position = mul(projection, mul(view, float4(world_pos, 1.0)));
    output.texcoord = input.texcoord;
    output.color = input.color;
    
    return output;
}
)";

// Billboard fragment shader
const char* const billboard_fragment = R"(
struct PSInput {
    float4 position : SV_Position;
    float2 texcoord : TEXCOORD0;
    float4 color : TEXCOORD1;
};

Texture2D billboard_texture : register(t0);
SamplerState billboard_sampler : register(s0);

float4 PSMain(PSInput input) : SV_Target {
    float4 tex_color = billboard_texture.Sample(billboard_sampler, input.texcoord);
    return tex_color * input.color;
}
)";

// Effect/particle vertex shader
const char* const effect_vertex = R"(
struct VSInput {
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 texcoord : TEXCOORD0;
    float4 color : COLOR0;
};

struct VSOutput {
    float4 position : SV_Position;
    float2 texcoord : TEXCOORD0;
    float4 color : TEXCOORD1;
};

cbuffer Uniforms : register(b0) {
    float4x4 view_projection;
    float3 camera_right;
    float _padding1;
    float3 camera_up;
    float time;
};

VSOutput VSMain(VSInput input) {
    VSOutput output;
    
    // Particle billboarding
    float3 world_pos = input.position;
    
    output.position = mul(view_projection, float4(world_pos, 1.0));
    output.texcoord = input.texcoord;
    output.color = input.color;
    
    return output;
}
)";

// Effect/particle fragment shader (additive blending)
const char* const effect_fragment = R"(
struct PSInput {
    float4 position : SV_Position;
    float2 texcoord : TEXCOORD0;
    float4 color : TEXCOORD1;
};

Texture2D effect_texture : register(t0);
SamplerState effect_sampler : register(s0);

float4 PSMain(PSInput input) : SV_Target {
    float4 tex_color = effect_texture.Sample(effect_sampler, input.texcoord);
    return tex_color * input.color;
}
)";

// Grass instanced vertex shader
const char* const grass_vertex = R"(
struct VSInput {
    // Per-vertex data (grass blade mesh)
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 texcoord : TEXCOORD0;
    float4 color : COLOR0;
};

struct VSOutput {
    float4 position : SV_Position;
    float2 texcoord : TEXCOORD0;
    float4 color : TEXCOORD1;
    float fog_distance : TEXCOORD2;
};

cbuffer Uniforms : register(b0) {
    float4x4 view_projection;
    float3 camera_pos;
    float time;
    float wind_strength;
    float3 wind_direction;
};

VSOutput VSMain(VSInput input) {
    VSOutput output;
    
    // Simple grass animation
    float wind = sin(time * 2.0 + input.position.x * 0.5) * wind_strength;
    float3 world_pos = input.position;
    world_pos.x += wind * input.position.y; // Bend more at top
    
    output.position = mul(view_projection, float4(world_pos, 1.0));
    output.texcoord = input.texcoord;
    output.color = input.color;
    output.fog_distance = length(world_pos - camera_pos);
    
    return output;
}
)";

// Grass fragment shader
const char* const grass_fragment = R"(
struct PSInput {
    float4 position : SV_Position;
    float2 texcoord : TEXCOORD0;
    float4 color : TEXCOORD1;
    float fog_distance : TEXCOORD2;
};

cbuffer Uniforms : register(b0) {
    float3 fog_color;
    float fog_start;
    float fog_end;
    int fog_enabled;
    int _padding[2];
};

Texture2D grass_texture : register(t0);
SamplerState grass_sampler : register(s0);

float4 PSMain(PSInput input) : SV_Target {
    float4 tex_color = grass_texture.Sample(grass_sampler, input.texcoord);
    
    // Alpha test for grass edges
    if (tex_color.a < 0.5) {
        discard;
    }
    
    float3 result = tex_color.rgb * input.color.rgb;
    
    // Apply fog
    if (fog_enabled == 1) {
        float fog_factor = saturate((input.fog_distance - fog_start) / (fog_end - fog_start));
        result = lerp(result, fog_color, fog_factor);
    }
    
    return float4(result, tex_color.a);
}
)";

// Text vertex shader
const char* const text_vertex = R"(
struct VSInput {
    float2 position : POSITION;
    float2 texcoord : TEXCOORD0;
    float4 color : COLOR0;
};

struct VSOutput {
    float4 position : SV_Position;
    float2 texcoord : TEXCOORD0;
    float4 color : TEXCOORD1;
};

cbuffer Uniforms : register(b0) {
    float4x4 projection;
};

VSOutput VSMain(VSInput input) {
    VSOutput output;
    output.position = mul(projection, float4(input.position, 0.0, 1.0));
    output.texcoord = input.texcoord;
    output.color = input.color;
    return output;
}
)";

// Text fragment shader
const char* const text_fragment = R"(
struct PSInput {
    float4 position : SV_Position;
    float2 texcoord : TEXCOORD0;
    float4 color : TEXCOORD1;
};

Texture2D font_atlas : register(t0);
SamplerState font_sampler : register(s0);

float4 PSMain(PSInput input) : SV_Target {
    float alpha = font_atlas.Sample(font_sampler, input.texcoord).r;
    return float4(input.color.rgb, input.color.a * alpha);
}
)";

// Grid debug vertex shader
const char* const grid_vertex = R"(
struct VSInput {
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 texcoord : TEXCOORD0;
    float4 color : COLOR0;
};

struct VSOutput {
    float4 position : SV_Position;
    float4 color : TEXCOORD0;
};

cbuffer Uniforms : register(b0) {
    float4x4 view_projection;
};

VSOutput VSMain(VSInput input) {
    VSOutput output;
    output.position = mul(view_projection, float4(input.position, 1.0));
    output.color = input.color;
    return output;
}
)";

// Grid debug fragment shader
const char* const grid_fragment = R"(
struct PSInput {
    float4 position : SV_Position;
    float4 color : TEXCOORD0;
};

float4 PSMain(PSInput input) : SV_Target {
    return input.color;
}
)";

// Skinned model vertex shader
const char* const skinned_model_vertex = R"(
struct VSInput {
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 texcoord : TEXCOORD0;
    float4 color : COLOR0;
    uint4 joints : BLENDINDICES;
    float4 weights : BLENDWEIGHT;
};

struct VSOutput {
    float4 position : SV_Position;
    float3 frag_pos : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float2 texcoord : TEXCOORD2;
    float4 color : TEXCOORD3;
    float fog_distance : TEXCOORD4;
    float4 light_space_pos : TEXCOORD5;
};

cbuffer CameraUniforms : register(b0) {
    float4x4 model;
    float4x4 view;
    float4x4 projection;
    float3 camera_pos;
    float _padding;
    float4x4 light_space_matrix;
};

cbuffer BoneUniforms : register(b1) {
    float4x4 bone_matrices[64];
};

VSOutput VSMain(VSInput input) {
    VSOutput output;
    
    // Skinning
    float4x4 skin_matrix = 
        bone_matrices[input.joints.x] * input.weights.x +
        bone_matrices[input.joints.y] * input.weights.y +
        bone_matrices[input.joints.z] * input.weights.z +
        bone_matrices[input.joints.w] * input.weights.w;
    
    float4 skinned_pos = mul(skin_matrix, float4(input.position, 1.0));
    float3 skinned_normal = mul((float3x3)skin_matrix, input.normal);
    
    float4 world_pos = mul(model, skinned_pos);
    output.frag_pos = world_pos.xyz;
    output.normal = mul((float3x3)model, skinned_normal);
    output.texcoord = input.texcoord;
    output.color = input.color;
    output.fog_distance = length(world_pos.xyz - camera_pos);
    output.light_space_pos = mul(light_space_matrix, world_pos);
    output.position = mul(projection, mul(view, world_pos));
    
    return output;
}
)";

// Skinned shadow vertex shader
const char* const skinned_shadow_vertex = R"(
struct VSInput {
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 texcoord : TEXCOORD0;
    float4 color : COLOR0;
    uint4 joints : BLENDINDICES;
    float4 weights : BLENDWEIGHT;
};

struct VSOutput {
    float4 position : SV_Position;
};

cbuffer Uniforms : register(b0) {
    float4x4 light_space_matrix;
    float4x4 model;
};

cbuffer BoneUniforms : register(b1) {
    float4x4 bone_matrices[64];
};

VSOutput VSMain(VSInput input) {
    VSOutput output;
    
    // Skinning
    float4x4 skin_matrix = 
        bone_matrices[input.joints.x] * input.weights.x +
        bone_matrices[input.joints.y] * input.weights.y +
        bone_matrices[input.joints.z] * input.weights.z +
        bone_matrices[input.joints.w] * input.weights.w;
    
    float4 skinned_pos = mul(skin_matrix, float4(input.position, 1.0));
    float4 world_pos = mul(model, skinned_pos);
    output.position = mul(light_space_matrix, world_pos);
    
    return output;
}
)";

} // namespace hlsl

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
    
    SDL_Log("PipelineRegistry: Initialized with swapchain format %d", 
            static_cast<int>(swapchain_format_));
    
    return true;
}

void PipelineRegistry::shutdown() {
    pipelines_.clear();
    shaders_.clear();
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
        case PipelineType::Shadow:
            pipeline = create_shadow_pipeline();
            break;
        case PipelineType::SkinnedShadow:
            pipeline = create_skinned_shadow_pipeline();
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
    shaders_.clear();
    SDL_Log("PipelineRegistry: All pipelines invalidated");
}

void PipelineRegistry::set_swapchain_format(SDL_GPUTextureFormat format) {
    if (format != swapchain_format_) {
        swapchain_format_ = format;
        invalidate_all();
    }
}

GPUShader* PipelineRegistry::get_or_create_shader(
    const std::string& name,
    ShaderStage stage,
    const std::string& hlsl_source,
    const std::string& entry_point,
    const ShaderResources& resources
) {
    // Check cache
    auto it = shaders_.find(name);
    if (it != shaders_.end()) {
        return it->second.get();
    }

    // Compile shader
    auto shader = GPUShader::compile_from_hlsl(
        *device_, hlsl_source, stage, entry_point, resources);
    
    if (!shader) {
        SDL_Log("PipelineRegistry: Failed to compile shader '%s'", name.c_str());
        return nullptr;
    }

    auto* result = shader.get();
    shaders_[name] = std::move(shader);
    return result;
}

// =============================================================================
// Pipeline Creation Methods
// =============================================================================

std::unique_ptr<GPUPipeline> PipelineRegistry::create_model_pipeline() {
    // Compile shaders
    ShaderResources vs_resources;
    vs_resources.num_uniform_buffers = 1;
    
    ShaderResources fs_resources;
    fs_resources.num_uniform_buffers = 1;
    fs_resources.num_samplers = 2;
    
    auto* vs = get_or_create_shader("model_vs", ShaderStage::Vertex,
                                     hlsl::model_vertex, "VSMain", vs_resources);
    auto* fs = get_or_create_shader("model_fs", ShaderStage::Fragment,
                                     hlsl::model_fragment, "PSMain", fs_resources);
    
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
    fs_resources.num_samplers = 2;
    
    auto* vs = get_or_create_shader("skinned_model_vs", ShaderStage::Vertex,
                                     hlsl::skinned_model_vertex, "VSMain", vs_resources);
    auto* fs = get_or_create_shader("model_fs", ShaderStage::Fragment,
                                     hlsl::model_fragment, "PSMain", fs_resources);
    
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
    fs_resources.num_samplers = 3;
    
    auto* vs = get_or_create_shader("terrain_vs", ShaderStage::Vertex,
                                     hlsl::terrain_vertex, "VSMain", vs_resources);
    auto* fs = get_or_create_shader("terrain_fs", ShaderStage::Fragment,
                                     hlsl::terrain_fragment, "PSMain", fs_resources);
    
    if (!vs || !fs) return nullptr;

    PipelineConfig config;
    config.vertex_shader = vs->handle();
    config.fragment_shader = fs->handle();
    config.with_vertex3d().opaque();
    config.color_format = swapchain_format_;
    config.depth_format = depth_format_;
    
    return GPUPipeline::create(*device_, config);
}

std::unique_ptr<GPUPipeline> PipelineRegistry::create_skybox_pipeline() {
    ShaderResources vs_resources;
    vs_resources.num_uniform_buffers = 1;
    
    ShaderResources fs_resources;
    fs_resources.num_uniform_buffers = 1;
    
    auto* vs = get_or_create_shader("skybox_vs", ShaderStage::Vertex,
                                     hlsl::skybox_vertex, "VSMain", vs_resources);
    auto* fs = get_or_create_shader("skybox_fs", ShaderStage::Fragment,
                                     hlsl::skybox_fragment, "PSMain", fs_resources);
    
    if (!vs || !fs) return nullptr;

    PipelineConfig config;
    config.vertex_shader = vs->handle();
    config.fragment_shader = fs->handle();
    config.with_vertex3d().opaque().cull_front(); // Render inside of cube
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
    
    auto* vs = get_or_create_shader("grid_vs", ShaderStage::Vertex,
                                     hlsl::grid_vertex, "VSMain", vs_resources);
    auto* fs = get_or_create_shader("grid_fs", ShaderStage::Fragment,
                                     hlsl::grid_fragment, "PSMain", fs_resources);
    
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
    vs_resources.num_uniform_buffers = 1;
    
    ShaderResources fs_resources;
    fs_resources.num_uniform_buffers = 1;
    fs_resources.num_samplers = 1;
    
    auto* vs = get_or_create_shader("ui_vs", ShaderStage::Vertex,
                                     hlsl::ui_vertex, "VSMain", vs_resources);
    auto* fs = get_or_create_shader("ui_fs", ShaderStage::Fragment,
                                     hlsl::ui_fragment, "PSMain", fs_resources);
    
    if (!vs || !fs) return nullptr;

    PipelineConfig config;
    config.vertex_shader = vs->handle();
    config.fragment_shader = fs->handle();
    config.with_vertex2d().alpha_blended().no_depth();
    config.color_format = swapchain_format_;
    
    return GPUPipeline::create(*device_, config);
}

std::unique_ptr<GPUPipeline> PipelineRegistry::create_text_pipeline() {
    ShaderResources vs_resources;
    vs_resources.num_uniform_buffers = 1;
    
    ShaderResources fs_resources;
    fs_resources.num_samplers = 1;
    
    auto* vs = get_or_create_shader("text_vs", ShaderStage::Vertex,
                                     hlsl::text_vertex, "VSMain", vs_resources);
    auto* fs = get_or_create_shader("text_fs", ShaderStage::Fragment,
                                     hlsl::text_fragment, "PSMain", fs_resources);
    
    if (!vs || !fs) return nullptr;

    PipelineConfig config;
    config.vertex_shader = vs->handle();
    config.fragment_shader = fs->handle();
    config.with_vertex2d().alpha_blended().no_depth();
    config.color_format = swapchain_format_;
    
    return GPUPipeline::create(*device_, config);
}

std::unique_ptr<GPUPipeline> PipelineRegistry::create_billboard_pipeline() {
    ShaderResources vs_resources;
    vs_resources.num_uniform_buffers = 1;
    
    ShaderResources fs_resources;
    fs_resources.num_samplers = 1;
    
    auto* vs = get_or_create_shader("billboard_vs", ShaderStage::Vertex,
                                     hlsl::billboard_vertex, "VSMain", vs_resources);
    auto* fs = get_or_create_shader("billboard_fs", ShaderStage::Fragment,
                                     hlsl::billboard_fragment, "PSMain", fs_resources);
    
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
    
    auto* vs = get_or_create_shader("effect_vs", ShaderStage::Vertex,
                                     hlsl::effect_vertex, "VSMain", vs_resources);
    auto* fs = get_or_create_shader("effect_fs", ShaderStage::Fragment,
                                     hlsl::effect_fragment, "PSMain", fs_resources);
    
    if (!vs || !fs) return nullptr;

    PipelineConfig config;
    config.vertex_shader = vs->handle();
    config.fragment_shader = fs->handle();
    config.with_vertex3d().additive().no_cull();
    config.depth_write_enable = false; // Don't write depth for particles
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
    
    auto* vs = get_or_create_shader("grass_vs", ShaderStage::Vertex,
                                     hlsl::grass_vertex, "VSMain", vs_resources);
    auto* fs = get_or_create_shader("grass_fs", ShaderStage::Fragment,
                                     hlsl::grass_fragment, "PSMain", fs_resources);
    
    if (!vs || !fs) return nullptr;

    PipelineConfig config;
    config.vertex_shader = vs->handle();
    config.fragment_shader = fs->handle();
    config.with_vertex3d().opaque().no_cull(); // Double-sided grass
    config.color_format = swapchain_format_;
    config.depth_format = depth_format_;
    
    return GPUPipeline::create(*device_, config);
}

std::unique_ptr<GPUPipeline> PipelineRegistry::create_shadow_pipeline() {
    ShaderResources vs_resources;
    vs_resources.num_uniform_buffers = 1;
    
    ShaderResources fs_resources;
    // No fragment resources for depth-only
    
    auto* vs = get_or_create_shader("shadow_vs", ShaderStage::Vertex,
                                     hlsl::shadow_vertex, "VSMain", vs_resources);
    auto* fs = get_or_create_shader("shadow_fs", ShaderStage::Fragment,
                                     hlsl::shadow_fragment, "PSMain", fs_resources);
    
    if (!vs || !fs) return nullptr;

    PipelineConfig config;
    config.vertex_shader = vs->handle();
    config.fragment_shader = fs->handle();
    config.with_vertex3d().opaque();
    config.has_depth_target = true;
    config.depth_format = depth_format_;
    // Shadow maps don't need color targets
    config.color_format = SDL_GPU_TEXTUREFORMAT_INVALID;
    // Depth bias to reduce shadow acne
    config.with_depth_bias(1.0f, 1.5f);
    // Cull front faces to reduce peter-panning
    config.cull_mode = SDL_GPU_CULLMODE_FRONT;
    
    return GPUPipeline::create(*device_, config);
}

std::unique_ptr<GPUPipeline> PipelineRegistry::create_skinned_shadow_pipeline() {
    ShaderResources vs_resources;
    vs_resources.num_uniform_buffers = 2; // Light space + Bones
    
    ShaderResources fs_resources;
    // No fragment resources for depth-only
    
    auto* vs = get_or_create_shader("skinned_shadow_vs", ShaderStage::Vertex,
                                     hlsl::skinned_shadow_vertex, "VSMain", vs_resources);
    auto* fs = get_or_create_shader("shadow_fs", ShaderStage::Fragment,
                                     hlsl::shadow_fragment, "PSMain", fs_resources);
    
    if (!vs || !fs) return nullptr;

    PipelineConfig config;
    config.vertex_shader = vs->handle();
    config.fragment_shader = fs->handle();
    config.with_skinned_vertex().opaque();
    config.has_depth_target = true;
    config.depth_format = depth_format_;
    config.color_format = SDL_GPU_TEXTUREFORMAT_INVALID;
    config.with_depth_bias(1.0f, 1.5f);
    config.cull_mode = SDL_GPU_CULLMODE_FRONT;
    
    return GPUPipeline::create(*device_, config);
}

} // namespace mmo::gpu
