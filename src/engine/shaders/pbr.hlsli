// pbr.hlsli - Cook-Torrance microfacet BRDF (GGX + Smith + Schlick)
// with Burley diffuse. All inputs are linear; output is linear radiance
// contribution from a single directional light.

#ifndef PBR_HLSLI
#define PBR_HLSLI

#ifndef PI_PBR
#define PI_PBR 3.14159265358979323846
#endif

// GGX/Trowbridge-Reitz normal distribution.
float ndfGGX(float NdotH, float roughness) {
    float a  = roughness * roughness;
    float a2 = a * a;
    float d  = (NdotH * NdotH) * (a2 - 1.0) + 1.0;
    return a2 / max(PI_PBR * d * d, 1e-7);
}

// Smith correlated visibility: V = G2 / (4 * NdotV * NdotL).
// Returns the already-divided visibility term; multiply by F and D for full spec.
float visibilitySmithGGX(float NdotV, float NdotL, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float ggxV = NdotL * sqrt(NdotV * NdotV * (1.0 - a2) + a2);
    float ggxL = NdotV * sqrt(NdotL * NdotL * (1.0 - a2) + a2);
    return 0.5 / max(ggxV + ggxL, 1e-5);
}

// Schlick Fresnel with roughness-aware grazing term (used by IBL later).
float3 fresnelSchlick(float cosTheta, float3 F0) {
    float f = pow(1.0 - cosTheta, 5.0);
    return F0 + (1.0 - F0) * f;
}

float3 fresnelSchlickRoughness(float cosTheta, float3 F0, float roughness) {
    float f = pow(1.0 - cosTheta, 5.0);
    return F0 + (max((1.0 - roughness).xxx, F0) - F0) * f;
}

// Burley (Disney) diffuse. Energy-conserving wrap that darkens grazing.
float3 diffuseBurley(float3 baseColor, float roughness, float NdotV, float NdotL, float LdotH) {
    float fd90 = 0.5 + 2.0 * LdotH * LdotH * roughness;
    float lightScatter = 1.0 + (fd90 - 1.0) * pow(1.0 - NdotL, 5.0);
    float viewScatter  = 1.0 + (fd90 - 1.0) * pow(1.0 - NdotV, 5.0);
    return baseColor * (lightScatter * viewScatter / PI_PBR);
}

// Single directional-light contribution.
// normal, view, light: world-space unit vectors (view points from surface to camera,
// light points from surface to light).
// lightColor: linear radiance at the surface (already shadow-modulated by caller if wanted).
// baseColor: linear albedo.
// metallic, roughness: glTF convention (metallic in [0,1], perceptual roughness in [0,1]).
// ao: baked occlusion multiplier applied to diffuse only.
float3 evaluatePBR(float3 normal, float3 view, float3 light, float3 lightColor,
                    float3 baseColor, float metallic, float roughness, float ao) {
    float3 N = normal;
    float3 V = view;
    float3 L = light;
    float3 H = normalize(V + L);

    float NdotV = max(dot(N, V), 1e-4);
    float NdotL = max(dot(N, L), 0.0);
    if (NdotL <= 0.0) {
        return float3(0, 0, 0);
    }
    float NdotH = max(dot(N, H), 0.0);
    float LdotH = max(dot(L, H), 0.0);

    float3 F0 = lerp(float3(0.04, 0.04, 0.04), baseColor, metallic);

    float  D = ndfGGX(NdotH, roughness);
    float  Vv = visibilitySmithGGX(NdotV, NdotL, roughness);
    float3 F = fresnelSchlick(LdotH, F0);

    float3 spec = D * Vv * F;

    // Energy conservation: metals have no diffuse.
    float3 kd = (1.0 - F) * (1.0 - metallic);
    float3 diff = kd * diffuseBurley(baseColor, roughness, NdotV, NdotL, LdotH) * ao;

    return (diff + spec) * lightColor * NdotL;
}

// Constant-ambient IBL placeholder. Replace with split-sum IBL when probes land.
float3 iblAmbient(float3 baseColor, float metallic, float ao, float3 ambientColor) {
    float3 diffuseAmbient = baseColor * (1.0 - metallic);
    return diffuseAmbient * ambientColor * ao;
}

#endif // PBR_HLSLI
