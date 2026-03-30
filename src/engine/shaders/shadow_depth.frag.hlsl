/**
 * Shadow Depth Fragment Shader
 *
 * Supports alpha-tested shadows for transparent textures (leaves, etc.).
 * When hasTexture is set and texture alpha < 0.5, the pixel is discarded
 * so light passes through transparent parts.
 */

struct PSInput {
    float4 position : SV_Position;
    float2 texCoord : TEXCOORD0;
};

[[vk::binding(0, 3)]]
cbuffer ShadowFragUniforms {
    int hasTexture;
    float _pad[3];
};

[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
Texture2D baseColorTexture;
[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
SamplerState baseColorSampler;

void PSMain(PSInput input) {
    if (hasTexture == 1) {
        float alpha = baseColorTexture.Sample(baseColorSampler, input.texCoord).a;
        if (alpha < 0.5) discard;
    }
}
