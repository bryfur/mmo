/**
 * Terrain Fragment Shader - SDL3 GPU API
 * 
 * Renders terrain with texture, shadows, SSAO, and fog.
 */

// Fragment input (from vertex shader)
struct PSInput {
    float4 position : SV_Position;
    float2 texCoord : TEXCOORD0;
    float4 vertexColor : TEXCOORD1;
    float3 fragPos : TEXCOORD2;
    float fogDistance : TEXCOORD3;
    float4 fragPosLightSpace : TEXCOORD4;
    float3 normal : TEXCOORD5;
};

// Uniform buffer - SDL3 GPU SPIR-V requires fragment uniforms in set 3
[[vk::binding(0, 3)]]
cbuffer LightingUniforms {
    float3 fogColor;
    float fogStart;
    float fogEnd;
    int shadowsEnabled;
    int ssaoEnabled;
    float _padding0;
    float3 lightDir;
    float _padding1;
    float2 screenSize;
    float2 _padding2;
};

// Texture and sampler bindings - SDL3 GPU SPIR-V requires fragment textures in set 2
[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
Texture2D grassTexture;
[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
SamplerState grassSampler;

[[vk::combinedImageSampler]][[vk::binding(1, 2)]]
Texture2D shadowMap;
[[vk::combinedImageSampler]][[vk::binding(1, 2)]]
SamplerState shadowSampler;

[[vk::combinedImageSampler]][[vk::binding(2, 2)]]
Texture2D ssaoTexture;
[[vk::combinedImageSampler]][[vk::binding(2, 2)]]
SamplerState ssaoSampler;

// Calculate shadow with PCF soft shadows
float calculateShadow(float4 fragPosLightSpace) {
    float3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords.xy = projCoords.xy * 0.5 + 0.5;
    // SDL3 GPU uses Y-down in texture coordinates
    projCoords.y = 1.0 - projCoords.y;
    
    if (projCoords.z > 1.0 || projCoords.x < 0.0 || projCoords.x > 1.0 || 
        projCoords.y < 0.0 || projCoords.y > 1.0) {
        // Treat out-of-bounds fragments as fully shadowed to avoid bright artifacts
        return 1.0;
    }
    
    float currentDepth = projCoords.z;
    float bias = 0.002;
    
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
    // Sample the seamless grass texture
    float4 texColor = grassTexture.Sample(grassSampler, input.texCoord);
    
    // Use texture color with subtle vertex color variation
    float3 color = texColor.rgb * lerp(float3(1.0, 1.0, 1.0), input.vertexColor.rgb, 0.3);
    
    // Calculate shadow
    float shadow = 0.0;
    if (shadowsEnabled == 1) {
        shadow = calculateShadow(input.fragPosLightSpace);
    }
    
    // Get SSAO value
    float ao = 1.0;
    if (ssaoEnabled == 1) {
        float2 screenUV = input.position.xy / screenSize;
        ao = ssaoTexture.Sample(ssaoSampler, screenUV).r;
    }
    
    // Simple directional lighting with shadow
    float3 lightDirection = normalize(-lightDir);
    float3 norm = normalize(input.normal);
    float diff = max(dot(norm, lightDirection), 0.0);
    
    // Lighting calculation
    float ambient = 0.4 * ao;
    float diffuse = diff * 0.6 * (1.0 - shadow * 0.6);
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
