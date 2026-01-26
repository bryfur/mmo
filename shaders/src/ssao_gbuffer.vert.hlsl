/**
 * SSAO G-Buffer Vertex Shader - SDL3 GPU API
 * 
 * Outputs view-space position and normal for SSAO calculation.
 */

// Vertex input
struct VSInput {
    float3 position : POSITION;
    float3 normal : NORMAL;
};

// Vertex output / Fragment input
struct VSOutput {
    float4 position : SV_Position;
    float3 fragPos : TEXCOORD0;
    float3 normal : TEXCOORD1;
};

// Uniform buffer slot 0 - Transform data
cbuffer GBufferUniforms : register(b0) {
    float4x4 model;
    float4x4 view;
    float4x4 projection;
};

VSOutput VSMain(VSInput input) {
    VSOutput output;
    
    float4 viewPos = mul(view, mul(model, float4(input.position, 1.0)));
    output.fragPos = viewPos.xyz;
    
    // Transform normal to view space
    float3x3 normalMatrix = (float3x3)transpose(mul(view, model));
    output.normal = mul(normalMatrix, input.normal);
    
    output.position = mul(projection, viewPos);
    
    return output;
}
