/**
 * UI Fragment Shader - SDL3 GPU API
 *
 * Renders UI elements with optional texture support.
 * When has_texture is 0, uses vertex color directly.
 * When has_texture is 1, multiplies vertex color by texture sample.
 */

// Fragment input (from vertex shader)
// Explicit locations must match VSOutput in ui.vert.hlsl
struct PSInput {
    float4 position : SV_Position;
    [[vk::location(0)]] float2 texcoord : TEXCOORD0;
    [[vk::location(1)]] float4 color : TEXCOORD1;
};

// Uniform buffer - SDL3 GPU SPIR-V requires fragment uniforms in set 3
// See SDL_CreateGPUShader docs: "For fragment shaders: 3: Uniform buffers"
[[vk::binding(0, 3)]]
cbuffer UIFragmentUniforms {
    int has_texture;
    int _padding[3];
};

// Texture and sampler in set 2 (fragment textures/samplers)
// SDL3 GPU expects combined texture-sampler, use same binding
[[vk::combinedImageSampler]]
[[vk::binding(0, 2)]]
Texture2D ui_texture;
[[vk::combinedImageSampler]]
[[vk::binding(0, 2)]]
SamplerState ui_sampler;

float4 PSMain(PSInput input) : SV_Target {
    float4 color = input.color;

    if (has_texture == 1) {
        color *= ui_texture.Sample(ui_sampler, input.texcoord);
    }

    return color;
}
