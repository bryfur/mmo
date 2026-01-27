/**
 * UI Vertex Shader - SDL3 GPU API
 *
 * 2D vertex transformation for UI elements with texture support.
 * Vertex format: position (2), texcoord (2), color (4) = 8 floats
 *
 * IMPORTANT: The [[vk::location(N)]] attributes MUST match the vertex
 * attribute locations in get_vertex2d_attributes() (gpu_types.hpp):
 *   location 0 = position (FLOAT2)
 *   location 1 = texcoord (FLOAT2)
 *   location 2 = color (FLOAT4)
 */

// Vertex input - matches Vertex2D / UIVertex layout
// Explicit SPIRV locations ensure correct mapping after HLSL->SPIRV compilation
struct VSInput {
    [[vk::location(0)]] float2 position : POSITION;
    [[vk::location(1)]] float2 texcoord : TEXCOORD0;
    [[vk::location(2)]] float4 color : COLOR0;
};

// Vertex output / Fragment input
// Explicit locations for vertex-to-fragment interface
struct VSOutput {
    float4 position : SV_Position;
    [[vk::location(0)]] float2 texcoord : TEXCOORD0;
    [[vk::location(1)]] float4 color : TEXCOORD1;
};

// Uniform buffer - SDL3 GPU SPIR-V requires vertex uniforms in set 1
// See SDL_CreateGPUShader docs: "For vertex shaders: 1: Uniform buffers"
[[vk::binding(0, 1)]]
cbuffer UIUniforms {
    float2 screen_size;
    float2 _padding;
};

VSOutput VSMain(VSInput input) {
    VSOutput output;

    // Convert screen coords to NDC for Vulkan/SDL3 GPU
    // Screen coords: (0,0) at top-left, Y increases downward
    // Vulkan clip space: Y=+1 at top, Y=-1 at bottom (after viewport transform)
    // So: y=0 (screen top) -> ndc.y=+1 (clip top)
    //     y=height (screen bottom) -> ndc.y=-1 (clip bottom)
    float2 ndc;
    ndc.x = input.position.x / (screen_size.x * 0.5) - 1.0;
    ndc.y = 1.0 - input.position.y / (screen_size.y * 0.5);
    output.position = float4(ndc, 0.0, 1.0);
    output.texcoord = input.texcoord;
    output.color = input.color;

    return output;
}
