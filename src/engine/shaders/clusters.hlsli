// clusters.hlsli - clustered forward+ shader-side helpers.
// Layout matches render::lighting::ClusterGrid. See light_cluster.cpp for
// the C++ side that builds + uploads these buffers.

#ifndef CLUSTERS_HLSLI
#define CLUSTERS_HLSLI

#include "pbr.hlsli"

#define CLUSTER_DIM_X 16
#define CLUSTER_DIM_Y 9
#define CLUSTER_DIM_Z 24
#define CLUSTER_COUNT (CLUSTER_DIM_X * CLUSTER_DIM_Y * CLUSTER_DIM_Z)

struct ClusterParams {
    float4x4 view;
    float4x4 invProjection;
    float4 screenSize;     // (w, h, 1/w, 1/h)
    float4 zPlanes;        // (near, far, log(far/near), 1/log(far/near))
    uint4 gridDim;         // (X, Y, Z, totalLightCount)
    uint4 maxPerCluster;   // (max_lights_per_cluster, _, _, _)
};

struct PointLightGPU {
    float3 position; float radius;
    float3 color;    float intensity;
};

struct SpotLightGPU {
    float3 position;  float radius;
    float3 direction; float inner_cos;
    float3 color;     float outer_cos;
    float intensity;  float3 _pad;
};

struct LightHeader {
    uint type;          // 0 = point, 1 = spot
    uint payload_index;
};

// Maps positive view-space depth (distance forward of camera) to a Z slice.
uint clusterZSlice(float view_depth, ClusterParams cp) {
    float depth = max(view_depth, cp.zPlanes.x);
    float t = log(depth / cp.zPlanes.x) * cp.zPlanes.w;
    uint z = (uint)clamp(floor(t * (float)cp.gridDim.z), 0.0, (float)cp.gridDim.z - 1.0);
    return z;
}

uint clusterIndexFromScreen(float2 uv, float view_depth, ClusterParams cp) {
    uint cx = (uint)clamp(floor(uv.x * (float)cp.gridDim.x), 0.0, (float)cp.gridDim.x - 1.0);
    uint cy = (uint)clamp(floor(uv.y * (float)cp.gridDim.y), 0.0, (float)cp.gridDim.y - 1.0);
    uint cz = clusterZSlice(view_depth, cp);
    return (cz * cp.gridDim.y + cy) * cp.gridDim.x + cx;
}

// Smoothly fades light to zero at the radius and applies inverse-square law.
float pointFalloff(float dist, float radius) {
    float r = saturate(dist / max(radius, 1e-4));
    float r2 = r * r;
    float win = saturate(1.0 - r2 * r2);  // (1 - (d/R)^4) clamped
    return (win * win) / max(dist * dist, 1e-4);
}

float spotConeFactor(float3 L_to_surface, float3 spotDir, float inner_cos, float outer_cos) {
    float cd = dot(spotDir, -L_to_surface);
    return smoothstep(outer_cos, inner_cos, cd);
}

// Generic point/spot PBR contribution. Caller supplies surface params and the
// per-light attenuation factor; the BRDF is identical to evaluatePBR.
float3 evaluatePBRPoint(float3 N, float3 V, float3 L, float3 lightColor, float attenuation,
                        float3 baseColor, float metallic, float roughness, float ao) {
    return evaluatePBR(N, V, L, lightColor * attenuation,
                       baseColor, metallic, roughness, ao);
}

float3 evaluatePBRSpot(float3 N, float3 V, float3 L, float3 lightColor,
                       float attenuation, float cone,
                       float3 baseColor, float metallic, float roughness, float ao) {
    return evaluatePBR(N, V, L, lightColor * (attenuation * cone),
                       baseColor, metallic, roughness, ao);
}

#endif // CLUSTERS_HLSLI
