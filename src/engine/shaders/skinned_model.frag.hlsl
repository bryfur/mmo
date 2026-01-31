/**
 * Skinned Model Fragment Shader - SDL3 GPU API
 *
 * Renders skinned 3D models with diffuse lighting and fog.
 * This is identical to the regular model fragment shader.
 */

// Fragment input (from vertex shader)
struct PSInput {
    float4 position : SV_Position;
    float3 fragPos : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float2 texCoord : TEXCOORD2;
    float4 vertexColor : TEXCOORD3;
    float fogDistance : TEXCOORD4;
};

// Uniform buffer - SDL3 GPU SPIR-V requires fragment uniforms in set 3
[[vk::binding(0, 3)]]
cbuffer LightingUniforms {
    float3 lightDir;
    float _padding0;
    float3 lightColor;
    float _padding1;
    float3 ambientColor;
    float _padding2;
    float4 tintColor;
    float3 fogColor;
    float fogStart;
    float fogEnd;
    int hasTexture;
    int fogEnabled;
    float _padding3;
};

// Texture and sampler bindings - SDL3 GPU SPIR-V requires fragment textures in set 2
[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
Texture2D baseColorTexture;
[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
SamplerState baseColorSampler;

float4 PSMain(PSInput input) : SV_Target {
    // Normalize inputs
    float3 norm = normalize(input.normal);
    float3 lightDirection = normalize(-lightDir);

    // Diffuse lighting
    float diff = max(dot(norm, lightDirection), 0.0);
    float3 diffuse = diff * lightColor;

    // Combine lighting
    float3 lighting = ambientColor + diffuse;

    // Get base color from texture or vertex color
    float4 baseColor;
    if (hasTexture == 1) {
        baseColor = baseColorTexture.Sample(baseColorSampler, input.texCoord);
    } else {
        baseColor = input.vertexColor * tintColor;
    }

    float3 result = lighting * baseColor.rgb;

    // Slight rim lighting for better visibility
    float3 viewDir = normalize(-input.fragPos);
    float rim = 1.0 - max(dot(viewDir, norm), 0.0);
    rim = smoothstep(0.6, 1.0, rim);
    result += rim * 0.3 * baseColor.rgb;

    // Apply distance fog
    if (fogEnabled == 1) {
        float fogFactor = saturate((input.fogDistance - fogStart) / (fogEnd - fogStart));
        // Use exponential falloff for more natural look
        fogFactor = 1.0 - exp(-fogFactor * 2.0);
        result = lerp(result, fogColor, fogFactor);
    }

    return float4(result, baseColor.a);
}
