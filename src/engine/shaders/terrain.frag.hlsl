/**
 * Terrain Fragment Shader - SDL3 GPU API
 *
 * Renders terrain with texture and fog.
 */

// Fragment input (from vertex shader)
struct PSInput {
    float4 position : SV_Position;
    float2 texCoord : TEXCOORD0;
    float4 vertexColor : TEXCOORD1;
    float3 fragPos : TEXCOORD2;
    float fogDistance : TEXCOORD3;
    float3 normal : TEXCOORD4;
};

// Uniform buffer - SDL3 GPU SPIR-V requires fragment uniforms in set 3
[[vk::binding(0, 3)]]
cbuffer LightingUniforms {
    float3 fogColor;
    float fogStart;
    float fogEnd;
    float _padding0[3];
    float3 lightDir;
    float _padding1;
};

// Texture and sampler bindings - SDL3 GPU SPIR-V requires fragment textures in set 2
[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
Texture2D grassTexture;
[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
SamplerState grassSampler;

float4 PSMain(PSInput input) : SV_Target {
    // Sample the seamless grass texture
    float4 texColor = grassTexture.Sample(grassSampler, input.texCoord);

    // Use texture color with subtle vertex color variation
    float3 color = texColor.rgb * lerp(float3(1.0, 1.0, 1.0), input.vertexColor.rgb, 0.3);

    // Simple directional lighting
    float3 lightDirection = normalize(-lightDir);
    float3 norm = normalize(input.normal);
    float diff = max(dot(norm, lightDirection), 0.0);

    // Lighting calculation
    float ambient = 0.4;
    float diffuse = diff * 0.6;
    float light = ambient + diffuse;

    // Add height-based variation for subtle detail using a cheap, trig-free pattern
    float heightPattern = frac(input.fragPos.x * 0.001 + input.fragPos.z * 0.0017);
    float heightVariation = lerp(-1.0, 1.0, heightPattern);
    light *= 0.9 + 0.1 * heightVariation;

    color *= light;

    // Apply distance fog
    float fogFactor = saturate((input.fogDistance - fogStart) / (fogEnd - fogStart));
    fogFactor = 1.0 - exp(-fogFactor * 2.0);
    color = lerp(color, fogColor, fogFactor);

    return float4(color, 1.0);
}
