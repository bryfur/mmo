#pragma once

#include <SDL3/SDL_gpu.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <string>
#include <vector>
#include <memory>
#include <array>
#include <cstdint>

namespace mmo::gpu {

// Forward declarations
class GPUDevice;
class GPUBuffer;
class GPUTexture;
class GPUPipeline;
class GPUShader;

// =============================================================================
// Vertex Formats - Must match existing structures in the codebase
// =============================================================================

/// Standard 3D vertex for static meshes
struct Vertex3D {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texcoord;
    glm::vec4 color;
};

/// Skinned vertex for animated meshes with bone influences
struct SkinnedVertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texcoord;
    glm::vec4 color;
    uint8_t joints[4];
    float weights[4];
};

/// 2D vertex for UI rendering
struct Vertex2D {
    glm::vec2 position;
    glm::vec2 texcoord;
    glm::vec4 color;
};

/// Grass instance data for instanced rendering
struct GrassInstance {
    glm::vec3 position;
    float rotation;
    float scale;
    float color_variation;
};

// =============================================================================
// Uniform Buffer Structures - Must match shader layouts (std140 compatible)
// =============================================================================

/// Camera/view uniform block
struct alignas(16) CameraUniforms {
    glm::mat4 view;
    glm::mat4 projection;
    glm::vec3 camera_pos;
    float padding;
};

/// Per-model transform uniform block
struct alignas(16) ModelUniforms {
    glm::mat4 model;
    glm::vec4 tint;
};

/// Lighting uniform block
struct alignas(16) LightUniforms {
    glm::vec3 light_dir;
    float ambient;
    glm::vec3 light_color;
    float padding;
    glm::mat4 light_space_matrix;
};

/// Time and animation uniform block
struct alignas(16) TimeUniforms {
    float time;
    float delta_time;
    float padding[2];
};

/// Bone matrices for skeletal animation
struct alignas(16) BoneUniforms {
    static constexpr size_t MAX_BONES = 64;
    glm::mat4 bones[MAX_BONES];
};

// =============================================================================
// Texture Formats - Common format mappings
// =============================================================================

enum class TextureFormat {
    RGBA8,
    BGRA8,
    R8,
    R16,           // 16-bit unsigned normalized (for heightmaps)
    D32F,          // Depth 32-bit float
    D24S8,         // Depth 24 + Stencil 8
};

inline SDL_GPUTextureFormat to_sdl_format(TextureFormat format) {
    switch (format) {
        case TextureFormat::RGBA8: return SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        case TextureFormat::BGRA8: return SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
        case TextureFormat::R8:    return SDL_GPU_TEXTUREFORMAT_R8_UNORM;
        case TextureFormat::R16:   return SDL_GPU_TEXTUREFORMAT_R16_UNORM;
        case TextureFormat::D32F:  return SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
        case TextureFormat::D24S8: return SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT;
        default:                   return SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    }
}

// =============================================================================
// Blend Modes - Common blend presets
// =============================================================================

enum class BlendMode {
    None,           // No blending (opaque)
    Alpha,          // Standard alpha blending
    Additive,       // Additive blending (for effects)
    Multiply,       // Multiply blending
};

inline SDL_GPUColorTargetBlendState get_blend_state(BlendMode mode) {
    SDL_GPUColorTargetBlendState state = {};
    
    switch (mode) {
        case BlendMode::None:
            state.enable_blend = false;
            break;
            
        case BlendMode::Alpha:
            state.enable_blend = true;
            state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
            state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            state.color_blend_op = SDL_GPU_BLENDOP_ADD;
            state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
            break;
            
        case BlendMode::Additive:
            state.enable_blend = true;
            state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
            state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            state.color_blend_op = SDL_GPU_BLENDOP_ADD;
            state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
            break;
            
        case BlendMode::Multiply:
            state.enable_blend = true;
            state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_DST_COLOR;
            state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
            state.color_blend_op = SDL_GPU_BLENDOP_ADD;
            state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_DST_ALPHA;
            state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
            state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
            break;

        default:
            // Unknown blend mode - default to no blending (opaque)
            state.enable_blend = false;
            break;
    }
    
    state.color_write_mask = SDL_GPU_COLORCOMPONENT_R | SDL_GPU_COLORCOMPONENT_G | 
                              SDL_GPU_COLORCOMPONENT_B | SDL_GPU_COLORCOMPONENT_A;
    return state;
}

// =============================================================================
// Vertex Input Descriptions - For pipeline creation
// =============================================================================

/// Get vertex attributes for Vertex3D
inline std::vector<SDL_GPUVertexAttribute> get_vertex3d_attributes() {
    return {
        { 0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(Vertex3D, position) },
        { 1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(Vertex3D, normal) },
        { 2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, offsetof(Vertex3D, texcoord) },
        { 3, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(Vertex3D, color) },
    };
}

/// Get vertex buffer description for Vertex3D
inline SDL_GPUVertexBufferDescription get_vertex3d_buffer_desc() {
    return {
        .slot = 0,
        .pitch = sizeof(Vertex3D),
        .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX,
        .instance_step_rate = 0,
    };
}

/// Get vertex attributes for SkinnedVertex
inline std::vector<SDL_GPUVertexAttribute> get_skinned_vertex_attributes() {
    return {
        { 0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(SkinnedVertex, position) },
        { 1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(SkinnedVertex, normal) },
        { 2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, offsetof(SkinnedVertex, texcoord) },
        { 3, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(SkinnedVertex, color) },
        { 4, 0, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4, offsetof(SkinnedVertex, joints) },
        { 5, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(SkinnedVertex, weights) },
    };
}

/// Get vertex buffer description for SkinnedVertex
inline SDL_GPUVertexBufferDescription get_skinned_vertex_buffer_desc() {
    return {
        .slot = 0,
        .pitch = sizeof(SkinnedVertex),
        .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX,
        .instance_step_rate = 0,
    };
}

/// Get vertex attributes for Vertex2D
inline std::vector<SDL_GPUVertexAttribute> get_vertex2d_attributes() {
    return {
        { 0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, offsetof(Vertex2D, position) },
        { 1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, offsetof(Vertex2D, texcoord) },
        { 2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(Vertex2D, color) },
    };
}

/// Get vertex buffer description for Vertex2D
inline SDL_GPUVertexBufferDescription get_vertex2d_buffer_desc() {
    return {
        .slot = 0,
        .pitch = sizeof(Vertex2D),
        .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX,
        .instance_step_rate = 0,
    };
}

} // namespace mmo::gpu
