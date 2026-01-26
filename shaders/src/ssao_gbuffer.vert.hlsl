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
    float4x4 normalMatrix;  // Pre-computed inverse-transpose of model-view matrix (CPU-side)
};

VSOutput VSMain(VSInput input) {
    VSOutput output;
    
    float4 viewPos = mul(view, mul(model, float4(input.position, 1.0)));
    output.fragPos = viewPos.xyz;
    
    // Transform normal to view space using pre-computed normal matrix
    // normalMatrix should be inverse-transpose of model-view, computed on CPU
    float3x3 normalMat3x3 = (float3x3)normalMatrix;
    output.normal = normalize(mul(normalMat3x3, input.normal));
    
    output.position = mul(projection, viewPos);
    
    return output;
}
