/**
 * Model Fragment Shader - SDL3 GPU API
 * 
 * Renders 3D models with diffuse lighting, shadows, SSAO, and fog.
 */

// Fragment input (from vertex shader)
struct PSInput {
    float4 position : SV_Position;
    float3 fragPos : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float2 texCoord : TEXCOORD2;
    float4 vertexColor : TEXCOORD3;
    float fogDistance : TEXCOORD4;
    float4 fragPosLightSpace : TEXCOORD5;
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
    int shadowsEnabled;
    int ssaoEnabled;
    int fogEnabled;
    float _padding3;
    float2 screenSize;
    float2 _padding4;
};

// Texture and sampler bindings - SDL3 GPU SPIR-V requires fragment textures in set 2
[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
Texture2D baseColorTexture;
[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
SamplerState baseColorSampler;

[[vk::combinedImageSampler]][[vk::binding(1, 2)]]
Texture2D shadowMap;
[[vk::combinedImageSampler]][[vk::binding(1, 2)]]
SamplerState shadowSampler;

[[vk::combinedImageSampler]][[vk::binding(2, 2)]]
Texture2D ssaoTexture;
[[vk::combinedImageSampler]][[vk::binding(2, 2)]]
SamplerState ssaoSampler;

// Calculate shadow with PCF soft shadows
float calculateShadow(float4 fragPosLightSpace, float3 normal, float3 lightDirection) {
    // Perspective divide
    float3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    
    // Transform to [0,1] range
    projCoords.xy = projCoords.xy * 0.5 + 0.5;
    // SDL3 GPU uses Y-down in texture coordinates
    projCoords.y = 1.0 - projCoords.y;
    
    // Check if outside shadow map
    if (projCoords.z > 1.0 || projCoords.x < 0.0 || projCoords.x > 1.0 || 
        projCoords.y < 0.0 || projCoords.y > 1.0) {
        // Treat areas outside the shadow map as fully shadowed to avoid incorrect lighting
        return 1.0;
    }
    
    float currentDepth = projCoords.z;
    
    // Slope-scaled bias to reduce shadow acne
    float bias = max(0.005 * (1.0 - dot(normal, lightDirection)), 0.001);
    
    // PCF (Percentage-Closer Filtering) for soft shadows
    float shadow = 0.0;
    float2 texelSize;
    shadowMap.GetDimensions(texelSize.x, texelSize.y);
    texelSize = 1.0 / texelSize;
    
    [unroll]
    for (int x = -2; x <= 2; ++x) {
        [unroll]
        for (int y = -2; y <= 2; ++y) {
            float pcfDepth = shadowMap.Sample(shadowSampler, projCoords.xy + float2(x, y) * texelSize).r;
            shadow += currentDepth - bias > pcfDepth ? 1.0 : 0.0;
        }
    }
    shadow /= 25.0;
    
    return shadow;
}

float4 PSMain(PSInput input) : SV_Target {
    // Normalize inputs
    float3 norm = normalize(input.normal);
    float3 lightDirection = normalize(-lightDir);
    
    // Calculate shadow
    float shadow = 0.0;
    if (shadowsEnabled == 1) {
        shadow = calculateShadow(input.fragPosLightSpace, norm, lightDirection);
    }
    
    // Diffuse lighting (reduced when in shadow)
    float diff = max(dot(norm, lightDirection), 0.0);
    float3 diffuse = diff * lightColor * (1.0 - shadow * 0.7);
    
    // Get SSAO value
    float ao = 1.0;
    if (ssaoEnabled == 1) {
        float2 screenUV = input.position.xy / screenSize;
        ao = ssaoTexture.Sample(ssaoSampler, screenUV).r;
    }
    
    // Combine lighting with ambient occlusion
    float3 ambient = ambientColor * ao;
    float3 lighting = ambient + diffuse;
    
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
