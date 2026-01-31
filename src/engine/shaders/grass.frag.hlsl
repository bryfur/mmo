/**
 * Grass Fragment Shader - SDL3 GPU API
 *
 * Renders grass blades using vertex colors with fog support.
 */

struct PSInput {
    float4 position : SV_Position;
    float3 normal : TEXCOORD0;
    float2 texCoord : TEXCOORD1;
    float3 worldPos : TEXCOORD2;
    float4 color : TEXCOORD3;
};

// Uniform buffer - SDL3 GPU SPIR-V requires fragment uniforms in set 3
[[vk::binding(0, 3)]]
cbuffer GrassLightingUniforms {
    float3 cameraPos;
    float fogStart;
    float3 fogColor;
    float fogEnd;
    int fogEnabled;
    int _padding0;
    int _padding1;
    int _padding2;
};

float4 PSMain(PSInput input) : SV_Target {
    // Use vertex color directly
    float3 color = input.color.rgb;

    // Simple directional lighting
    float3 norm = normalize(input.normal);
    float3 lightDir = normalize(float3(0.3, -1.0, 0.5));
    float diff = max(dot(norm, -lightDir), 0.0);
    float3 ambient = float3(0.3, 0.35, 0.3);
    color *= ambient + diff * float3(1.0, 0.95, 0.9);

    // Fog based on distance from camera
    if (fogEnabled != 0) {
        float dist = length(input.worldPos - cameraPos);
        float fogFactor = saturate((dist - fogStart) / (fogEnd - fogStart));
        color = lerp(color, fogColor, fogFactor);
    }

    return float4(color, input.color.a);
}
