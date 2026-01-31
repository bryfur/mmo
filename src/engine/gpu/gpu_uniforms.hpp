#pragma once

#include <glm/glm.hpp>

namespace mmo::gpu {

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

} // namespace mmo::gpu
