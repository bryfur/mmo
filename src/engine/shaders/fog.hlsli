// fog.hlsli - shared fog helpers.
// All values in linear light; caller blends in linear space.

#ifndef FOG_HLSLI
#define FOG_HLSLI

float linearFog(float distance, float start, float end) {
    return saturate((distance - start) / max(end - start, 1e-5));
}

float exponentialFog(float distance, float density) {
    return 1.0 - exp(-distance * density);
}

float exponentialSquaredFog(float distance, float density) {
    float f = distance * density;
    return 1.0 - exp(-f * f);
}

// Legacy-compatible: linear factor with exp-shaped falloff; matches
// the previous inline formula used by model/terrain shaders.
float distanceFogFactor(float distance, float start, float end) {
    float t = linearFog(distance, start, end);
    return 1.0 - exp(-t * 2.0);
}

#endif // FOG_HLSLI
