#pragma once

#include <glm/glm.hpp>

namespace mmo::engine::gpu {

/**
 * Model vertex shader uniforms - matches model.vert.hlsl TransformUniforms (set 1, b0)
 * Used by: renderer.cpp, world_renderer.cpp, effect_renderer.cpp
 */
struct alignas(16) ModelTransformUniforms {
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 projection;
    glm::vec3 cameraPos;
    float _padding0 = 0.0f;
    glm::mat4 normalMatrix;
    int useSkinning = 0;
    float _padding1[3] = {0.0f, 0.0f, 0.0f};
};

/**
 * Model fragment shader uniforms - matches model.frag.hlsl LightingUniforms (set 3, b0)
 * Used by: renderer.cpp, world_renderer.cpp, effect_renderer.cpp
 */
struct alignas(16) ModelLightingUniforms {
    glm::vec3 lightDir;
    float _padding0 = 0.0f;
    glm::vec3 lightColor;
    float _padding1 = 0.0f;
    glm::vec3 ambientColor;
    float _padding2 = 0.0f;
    glm::vec4 tintColor;
    glm::vec3 fogColor;
    float fogStart;
    float fogEnd;
    int hasTexture;
    int fogEnabled;
    float _padding3 = 0.0f;
};

/**
 * Skybox fragment shader uniforms - matches skybox.frag.hlsl SkyUniforms (set 3, b0)
 */
struct alignas(16) SkyboxFragmentUniforms {
    glm::mat4 invViewProjection;
    float time;
    glm::vec3 sunDirection;
};

/**
 * Grid vertex shader uniforms (set 1, b0)
 */
struct alignas(16) GridVertexUniforms {
    glm::mat4 viewProjection;
};

/**
 * UI vertex shader uniforms (set 1, b0)
 */
struct alignas(16) UIScreenUniforms {
    float width;
    float height;
    float _padding[2] = {0.0f, 0.0f};
};

/**
 * UI fragment shader uniforms (set 3, b0)
 */
struct alignas(16) UIFragmentUniforms {
    int hasTexture;
    int _padding[3] = {0, 0, 0};
};

/**
 * Instanced model vertex shader camera uniforms - matches model_instanced.vert.hlsl (set 1, b0)
 */
struct alignas(16) InstancedCameraUniforms {
    glm::mat4 view;
    glm::mat4 projection;
    glm::vec3 cameraPos;
    float _padding0 = 0.0f;
};

/**
 * Instanced model fragment shader uniforms - matches model_instanced.frag.hlsl (set 3, b0)
 */
struct alignas(16) InstancedLightingUniforms {
    glm::vec3 lightDir;
    float _padding0 = 0.0f;
    glm::vec3 lightColor;
    float _padding1 = 0.0f;
    glm::vec3 ambientColor;
    float _padding2 = 0.0f;
    glm::vec3 fogColor;
    float fogStart;
    float fogEnd;
    int hasTexture;
    int fogEnabled;
    float _padding3 = 0.0f;
};

/**
 * Per-instance data for instanced model rendering (storage buffer).
 */
struct InstanceData {
    glm::mat4 model;
    glm::mat4 normalMatrix;
    glm::vec4 tint;
    float noFog;
    float _pad[3] = {0.0f, 0.0f, 0.0f};
};

/**
 * Per-instance data for instanced shadow rendering (storage buffer).
 */
struct ShadowInstanceData {
    glm::mat4 model;
};

/**
 * Instanced shadow vertex uniforms (set 1, b0)
 */
struct alignas(16) InstancedShadowUniforms {
    glm::mat4 lightViewProjection;
};

/**
 * Shadow depth pass - vertex uniforms for static models (set 1, b0)
 */
struct alignas(16) ShadowTransformUniforms {
    glm::mat4 lightViewProjection;
    glm::mat4 model;
};

/**
 * Shadow depth pass - vertex uniforms for terrain (set 1, b0)
 * Terrain vertices are already in world space, no model matrix needed.
 */
struct alignas(16) ShadowTerrainUniforms {
    glm::mat4 lightViewProjection;
};

/**
 * Shadow data for fragment shaders in the main pass (set 3, b1)
 * Contains cascade view-projection matrices and PCSS parameters.
 */
struct alignas(16) ShadowDataUniforms {
    glm::mat4 lightViewProjection[4];  // Per-cascade light-space matrices
    glm::vec4 cascadeSplits;           // View-space far depth per cascade
    float shadowMapResolution;
    float lightSize;                   // PCSS penumbra size
    float shadowEnabled;
    float _pad0 = 0.0f;
};

/**
 * GTAO pass fragment uniforms (set 3, b0)
 */
struct alignas(16) GTAOUniforms {
    glm::mat4 projection;
    glm::mat4 invProjection;
    glm::vec2 screenSize;
    glm::vec2 invScreenSize;
    float radius = 1.5f;
    float bias = 0.01f;
    int numDirections = 6;
    int numSteps = 3;
};

/**
 * Bilateral blur pass fragment uniforms (set 3, b0)
 */
struct alignas(16) BlurUniforms {
    glm::vec2 direction;
    glm::vec2 invScreenSize;
    float sharpness = 40.0f;
    float _padding[3] = {0.0f, 0.0f, 0.0f};
};

/**
 * Composite pass fragment uniforms (set 3, b0)
 */
struct alignas(16) CompositeUniforms {
    float aoStrength = 1.0f;
    float _padding[3] = {0.0f, 0.0f, 0.0f};
};

} // namespace mmo::engine::gpu
