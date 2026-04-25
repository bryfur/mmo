// cluster_lighting.hlsli - bindings + accumulator for clustered forward+ lights.
// Including shader must NOT also declare ClusterParams cbuffer / SSBO bindings;
// these slots (set 3, b2 + set 0, b0/1/2) are reserved for clustered lighting.

#ifndef CLUSTER_LIGHTING_HLSLI
#define CLUSTER_LIGHTING_HLSLI

#include "clusters.hlsli"

[[vk::binding(2, 3)]]
cbuffer ClusterParamsCB { ClusterParams clusterParams; };

[[vk::binding(0, 0)]]
ByteAddressBuffer lightDataBuf;

[[vk::binding(1, 0)]]
StructuredBuffer<uint2> clusterOffsetsBuf;

[[vk::binding(2, 0)]]
StructuredBuffer<uint> lightIndicesBuf;

LightHeader loadLightHeader(uint i) {
    // Layout: [LightDataHeader 16B][LightHeader * 256][PointLight * 256][SpotLight * 256]
    uint base = 16 + i * 8;
    uint2 raw = lightDataBuf.Load2(base);
    LightHeader h;
    h.type = raw.x;
    h.payload_index = raw.y;
    return h;
}

PointLightGPU loadPointLight(uint i) {
    uint base = 16 + 256 * 8 + i * 32;
    uint4 a = lightDataBuf.Load4(base);
    uint4 b = lightDataBuf.Load4(base + 16);
    PointLightGPU p;
    p.position  = asfloat(a.xyz);
    p.radius    = asfloat(a.w);
    p.color     = asfloat(b.xyz);
    p.intensity = asfloat(b.w);
    return p;
}

SpotLightGPU loadSpotLight(uint i) {
    uint base = 16 + 256 * 8 + 256 * 32 + i * 64;
    uint4 a = lightDataBuf.Load4(base);
    uint4 b = lightDataBuf.Load4(base + 16);
    uint4 c = lightDataBuf.Load4(base + 32);
    uint4 d = lightDataBuf.Load4(base + 48);
    SpotLightGPU s;
    s.position  = asfloat(a.xyz);
    s.radius    = asfloat(a.w);
    s.direction = asfloat(b.xyz);
    s.inner_cos = asfloat(b.w);
    s.color     = asfloat(c.xyz);
    s.outer_cos = asfloat(c.w);
    s.intensity = asfloat(d.x);
    s._pad      = float3(0, 0, 0);
    return s;
}

float3 accumulateClusterLights(float3 P, float3 N, float3 V,
                                float3 baseColor, float metallic, float roughness, float ao,
                                float view_depth, float2 screen_uv) {
    if (clusterParams.gridDim.w == 0u) return float3(0, 0, 0);
    uint cidx = clusterIndexFromScreen(screen_uv, view_depth, clusterParams);
    uint2 oc = clusterOffsetsBuf[cidx];
    uint offset = oc.x;
    uint count  = oc.y;
    float3 acc = float3(0, 0, 0);
    for (uint i = 0; i < count; ++i) {
        uint header_idx = lightIndicesBuf[offset + i];
        LightHeader h = loadLightHeader(header_idx);
        if (h.type == 0u) {
            PointLightGPU pl = loadPointLight(h.payload_index);
            float3 to_light = pl.position - P;
            float dist = length(to_light);
            if (dist > pl.radius) continue;
            float3 L = to_light / max(dist, 1e-4);
            float att = pointFalloff(dist, pl.radius);
            acc += evaluatePBRPoint(N, V, L, pl.color * pl.intensity, att,
                                    baseColor, metallic, roughness, ao);
        } else {
            SpotLightGPU sl = loadSpotLight(h.payload_index);
            float3 to_light = sl.position - P;
            float dist = length(to_light);
            if (dist > sl.radius) continue;
            float3 L = to_light / max(dist, 1e-4);
            float att = pointFalloff(dist, sl.radius);
            float cone = spotConeFactor(L, sl.direction, sl.inner_cos, sl.outer_cos);
            if (cone <= 0.0) continue;
            acc += evaluatePBRSpot(N, V, L, sl.color * sl.intensity, att, cone,
                                   baseColor, metallic, roughness, ao);
        }
    }
    return acc;
}

#endif // CLUSTER_LIGHTING_HLSLI
