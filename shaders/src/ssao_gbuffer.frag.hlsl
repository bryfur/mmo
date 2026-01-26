/**
 * SSAO G-Buffer Fragment Shader - SDL3 GPU API
 * 
 * Outputs view-space position and normal to separate render targets.
 */

// Fragment input (from vertex shader)
struct PSInput {
    float4 position : SV_Position;
    float3 fragPos : TEXCOORD0;
    float3 normal : TEXCOORD1;
};

// Multiple render targets
struct PSOutput {
    float3 gPosition : SV_Target0;
    float3 gNormal : SV_Target1;
};

PSOutput PSMain(PSInput input) {
    PSOutput output;
    
    output.gPosition = input.fragPos;
    output.gNormal = normalize(input.normal);
    
    return output;
}
