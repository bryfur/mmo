/**
 * Grid Debug Vertex Shader - SDL3 GPU API
 *
 * Simple vertex transformation for debug grid lines.
 */

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

// Uniform buffer - SDL3 GPU SPIR-V requires vertex uniforms in set 1
[[vk::binding(0, 1)]]
cbuffer Uniforms {
    float4x4 view_projection;
};

VSOutput VSMain(VSInput input) {
    VSOutput output;
    output.position = mul(view_projection, float4(input.position, 1.0));
    output.color = input.color;
    return output;
}
