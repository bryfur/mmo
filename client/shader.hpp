#pragma once

#include <string>
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace mmo {

class Shader {
public:
    Shader() = default;
    ~Shader();
    
    bool load(const std::string& vertex_src, const std::string& fragment_src);
    void use() const;
    
    void set_mat4(const std::string& name, const glm::mat4& mat) const;
    void set_vec2(const std::string& name, const glm::vec2& vec) const;
    void set_vec3(const std::string& name, const glm::vec3& vec) const;
    void set_vec4(const std::string& name, const glm::vec4& vec) const;
    void set_float(const std::string& name, float value) const;
    void set_int(const std::string& name, int value) const;
    
    GLuint id() const { return program_; }
    
private:
    GLuint program_ = 0;
    
    GLuint compile_shader(GLenum type, const std::string& source);
};

// Built-in shader sources
namespace shaders {

const char* const model_vertex = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoord;
layout (location = 3) in vec4 aColor;

out vec3 FragPos;
out vec3 Normal;
out vec2 TexCoord;
out vec4 VertexColor;
out float FogDistance;
out vec4 FragPosLightSpace;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform vec3 cameraPos;
uniform mat4 lightSpaceMatrix;

void main() {
    vec4 worldPos = model * vec4(aPos, 1.0);
    FragPos = worldPos.xyz;
    Normal = mat3(transpose(inverse(model))) * aNormal;
    TexCoord = aTexCoord;
    VertexColor = aColor;
    FogDistance = length(worldPos.xyz - cameraPos);
    FragPosLightSpace = lightSpaceMatrix * worldPos;
    gl_Position = projection * view * worldPos;
}
)";

const char* const model_fragment = R"(
#version 330 core
out vec4 FragColor;

in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoord;
in vec4 VertexColor;
in float FogDistance;
in vec4 FragPosLightSpace;

uniform vec3 lightDir;
uniform vec3 lightColor;
uniform vec3 ambientColor;
uniform vec4 tintColor;
uniform sampler2D baseColorTexture;
uniform int hasTexture;

// Shadow mapping
uniform sampler2D shadowMap;
uniform int shadowsEnabled;

// SSAO
uniform sampler2D ssaoTexture;
uniform int ssaoEnabled;
uniform vec2 screenSize;

// Fog uniforms
uniform vec3 fogColor;
uniform float fogStart;
uniform float fogEnd;
uniform int fogEnabled;

// Calculate shadow with PCF soft shadows
float calculateShadow(vec4 fragPosLightSpace, vec3 normal, vec3 lightDirection) {
    // Perspective divide
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords = projCoords * 0.5 + 0.5;
    
    // Check if outside shadow map
    if (projCoords.z > 1.0 || projCoords.x < 0.0 || projCoords.x > 1.0 || 
        projCoords.y < 0.0 || projCoords.y > 1.0) {
        return 0.0;
    }
    
    float currentDepth = projCoords.z;
    
    // Slope-scaled bias to reduce shadow acne
    float bias = max(0.005 * (1.0 - dot(normal, lightDirection)), 0.001);
    
    // PCF (Percentage-Closer Filtering) for soft shadows
    float shadow = 0.0;
    vec2 texelSize = 1.0 / textureSize(shadowMap, 0);
    for (int x = -2; x <= 2; ++x) {
        for (int y = -2; y <= 2; ++y) {
            float pcfDepth = texture(shadowMap, projCoords.xy + vec2(x, y) * texelSize).r;
            shadow += currentDepth - bias > pcfDepth ? 1.0 : 0.0;
        }
    }
    shadow /= 25.0;
    
    return shadow;
}

void main() {
    // Normalize inputs
    vec3 norm = normalize(Normal);
    vec3 lightDirection = normalize(-lightDir);
    
    // Calculate shadow
    float shadow = 0.0;
    if (shadowsEnabled == 1) {
        shadow = calculateShadow(FragPosLightSpace, norm, lightDirection);
    }
    
    // Diffuse lighting (reduced when in shadow)
    float diff = max(dot(norm, lightDirection), 0.0);
    vec3 diffuse = diff * lightColor * (1.0 - shadow * 0.7);
    
    // Get SSAO value
    float ao = 1.0;
    if (ssaoEnabled == 1) {
        vec2 screenUV = gl_FragCoord.xy / screenSize;
        ao = texture(ssaoTexture, screenUV).r;
    }
    
    // Combine lighting with ambient occlusion
    vec3 ambient = ambientColor * ao;
    vec3 lighting = ambient + diffuse;
    
    // Get base color from texture or vertex color
    vec4 baseColor;
    if (hasTexture == 1) {
        // Use texture color directly, no tint
        baseColor = texture(baseColorTexture, TexCoord);
    } else {
        baseColor = VertexColor * tintColor;
    }
    
    vec3 result = lighting * baseColor.rgb;
    
    // Slight rim lighting for better visibility
    vec3 viewDir = normalize(-FragPos);
    float rim = 1.0 - max(dot(viewDir, norm), 0.0);
    rim = smoothstep(0.6, 1.0, rim);
    result += rim * 0.3 * baseColor.rgb;
    
    // Apply distance fog
    if (fogEnabled == 1) {
        float fogFactor = clamp((FogDistance - fogStart) / (fogEnd - fogStart), 0.0, 1.0);
        // Use exponential falloff for more natural look
        fogFactor = 1.0 - exp(-fogFactor * 2.0);
        result = mix(result, fogColor, fogFactor);
    }
    
    FragColor = vec4(result, baseColor.a);
}
)";

const char* const grid_vertex = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec4 aColor;

out vec4 VertexColor;

uniform mat4 view;
uniform mat4 projection;

void main() {
    VertexColor = aColor;
    gl_Position = projection * view * vec4(aPos, 1.0);
}
)";

const char* const grid_fragment = R"(
#version 330 core
out vec4 FragColor;

in vec4 VertexColor;

void main() {
    FragColor = VertexColor;
}
)";

// Terrain shader with seamless texture tiling and shadows
const char* const terrain_vertex = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTexCoord;
layout (location = 2) in vec4 aColor;

out vec2 TexCoord;
out vec4 VertexColor;
out vec3 FragPos;
out float FogDistance;
out vec4 FragPosLightSpace;
out vec3 Normal;

uniform mat4 view;
uniform mat4 projection;
uniform vec3 cameraPos;
uniform mat4 lightSpaceMatrix;

void main() {
    FragPos = aPos;
    TexCoord = aTexCoord;
    VertexColor = aColor;
    FogDistance = length(aPos - cameraPos);
    FragPosLightSpace = lightSpaceMatrix * vec4(aPos, 1.0);
    
    // Calculate normal from height differences (approximation for terrain)
    Normal = vec3(0.0, 1.0, 0.0);
    
    gl_Position = projection * view * vec4(aPos, 1.0);
}
)";

const char* const terrain_fragment = R"(
#version 330 core
out vec4 FragColor;

in vec2 TexCoord;
in vec4 VertexColor;
in vec3 FragPos;
in float FogDistance;
in vec4 FragPosLightSpace;
in vec3 Normal;

uniform sampler2D grassTexture;
uniform vec3 fogColor;
uniform float fogStart;
uniform float fogEnd;

// Shadow mapping
uniform sampler2D shadowMap;
uniform int shadowsEnabled;
uniform vec3 lightDir;

// SSAO
uniform sampler2D ssaoTexture;
uniform int ssaoEnabled;
uniform vec2 screenSize;

// Calculate shadow with PCF soft shadows
float calculateShadow(vec4 fragPosLightSpace) {
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords = projCoords * 0.5 + 0.5;
    
    if (projCoords.z > 1.0 || projCoords.x < 0.0 || projCoords.x > 1.0 || 
        projCoords.y < 0.0 || projCoords.y > 1.0) {
        return 0.0;
    }
    
    float currentDepth = projCoords.z;
    float bias = 0.002;
    
    float shadow = 0.0;
    vec2 texelSize = 1.0 / textureSize(shadowMap, 0);
    for (int x = -2; x <= 2; ++x) {
        for (int y = -2; y <= 2; ++y) {
            float pcfDepth = texture(shadowMap, projCoords.xy + vec2(x, y) * texelSize).r;
            shadow += currentDepth - bias > pcfDepth ? 1.0 : 0.0;
        }
    }
    shadow /= 25.0;
    
    return shadow;
}

void main() {
    // Sample the seamless grass texture
    vec4 texColor = texture(grassTexture, TexCoord);
    
    // Use texture color with subtle vertex color variation
    vec3 color = texColor.rgb * mix(vec3(1.0), VertexColor.rgb, 0.3);
    
    // Calculate shadow
    float shadow = 0.0;
    if (shadowsEnabled == 1) {
        shadow = calculateShadow(FragPosLightSpace);
    }
    
    // Get SSAO value
    float ao = 1.0;
    if (ssaoEnabled == 1) {
        vec2 screenUV = gl_FragCoord.xy / screenSize;
        ao = texture(ssaoTexture, screenUV).r;
    }
    
    // Simple directional lighting with shadow
    vec3 lightDirection = normalize(-lightDir);
    vec3 norm = normalize(Normal);
    float diff = max(dot(norm, lightDirection), 0.0);
    
    // Lighting calculation
    float ambient = 0.4 * ao;
    float diffuse = diff * 0.6 * (1.0 - shadow * 0.6);
    float light = ambient + diffuse;
    
    // Also add height-based variation for subtle detail
    light *= 0.9 + 0.1 * sin(FragPos.x * 0.01) * cos(FragPos.z * 0.01);
    
    color *= light;
    
    // Apply distance fog
    float fogFactor = clamp((FogDistance - fogStart) / (fogEnd - fogStart), 0.0, 1.0);
    fogFactor = 1.0 - exp(-fogFactor * 2.0);
    color = mix(color, fogColor, fogFactor);
    
    FragColor = vec4(color, 1.0);
}
)";

const char* const ui_vertex = R"(
#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec4 aColor;

out vec4 VertexColor;

uniform mat4 projection;

void main() {
    VertexColor = aColor;
    gl_Position = projection * vec4(aPos, 0.0, 1.0);
}
)";

const char* const ui_fragment = R"(
#version 330 core
out vec4 FragColor;

in vec4 VertexColor;

void main() {
    FragColor = VertexColor;
}
)";

// 3D Billboard shader for health bars (depth-tested)
const char* const billboard_vertex = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec4 aColor;

out vec4 VertexColor;

uniform mat4 view;
uniform mat4 projection;
uniform vec3 worldPos;      // World position of billboard center
uniform vec2 size;          // Size in world units
uniform vec2 offset;        // Offset from center in local billboard space

void main() {
    VertexColor = aColor;
    
    // Get camera right and up vectors from view matrix
    vec3 cameraRight = vec3(view[0][0], view[1][0], view[2][0]);
    vec3 cameraUp = vec3(view[0][1], view[1][1], view[2][1]);
    
    // Billboard position: expand quad in camera space
    vec3 pos = worldPos 
             + cameraRight * (aPos.x * size.x + offset.x)
             + cameraUp * (aPos.y * size.y + offset.y);
    
    gl_Position = projection * view * vec4(pos, 1.0);
}
)";

const char* const billboard_fragment = R"(
#version 330 core
out vec4 FragColor;

in vec4 VertexColor;

void main() {
    FragColor = VertexColor;
}
)";

// Procedural skybox with mountains
const char* const skybox_vertex = R"(
#version 330 core
layout (location = 0) in vec3 aPos;

out vec3 WorldPos;

uniform mat4 view;
uniform mat4 projection;
uniform vec3 cameraPos;

void main() {
    // Position the skybox centered on camera
    WorldPos = aPos;
    vec4 pos = projection * mat4(mat3(view)) * vec4(aPos, 1.0);
    gl_Position = pos.xyww;  // Set z to w for maximum depth
}
)";

const char* const skybox_fragment = R"(
#version 330 core
out vec4 FragColor;

in vec3 WorldPos;

uniform float time;
uniform vec3 sunDirection;  // Direction TO sun (normalized)

// Simple hash function for noise
float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

// Value noise
float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    
    float a = hash(i);
    float b = hash(i + vec2(1.0, 0.0));
    float c = hash(i + vec2(0.0, 1.0));
    float d = hash(i + vec2(1.0, 1.0));
    
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

// FBM for mountains
float fbm(vec2 p) {
    float value = 0.0;
    float amplitude = 0.5;
    for (int i = 0; i < 5; i++) {
        value += amplitude * noise(p);
        p *= 2.0;
        amplitude *= 0.5;
    }
    return value;
}

void main() {
    vec3 dir = normalize(WorldPos);
    
    // Sun direction and sun disk calculation
    vec3 sunDir = normalize(sunDirection);
    float sunAngle = acos(clamp(dot(dir, sunDir), -1.0, 1.0));
    
    // Sun disk (small bright core)
    float sunDiskRadius = 0.02;  // Angular radius of sun disk
    float sunDisk = smoothstep(sunDiskRadius, sunDiskRadius * 0.5, sunAngle);
    
    // Sun corona/glow (larger soft glow around sun)
    float coronaRadius = 0.15;
    float corona = exp(-sunAngle * sunAngle / (coronaRadius * coronaRadius)) * 0.6;
    
    // Sun glare/rays (subtle light rays)
    float glareRadius = 0.4;
    float glare = exp(-sunAngle / glareRadius) * 0.3;
    
    // Sun color - warm yellow/white
    vec3 sunColor = vec3(1.0, 0.95, 0.85);
    vec3 coronaColor = vec3(1.0, 0.8, 0.5);
    vec3 glareColor = vec3(1.0, 0.9, 0.7);
    
    // Sky gradient - affected by sun position for more realistic lighting
    float horizon = smoothstep(-0.1, 0.3, dir.y);
    
    // Base sky colors - brighten based on sun height
    float sunHeight = sunDir.y;  // How high sun is in sky
    float dayFactor = clamp(sunHeight * 2.0 + 0.5, 0.0, 1.0);  // 0 at night, 1 at day
    
    // Day sky colors
    vec3 dayTop = vec3(0.2, 0.4, 0.8);       // Blue sky at top
    vec3 dayHorizon = vec3(0.5, 0.6, 0.75);  // Lighter at horizon
    
    // Night sky colors
    vec3 nightTop = vec3(0.02, 0.05, 0.12);      // Dark blue-black
    vec3 nightHorizon = vec3(0.08, 0.1, 0.18);   // Slightly lighter
    
    // Blend between day and night based on sun height
    vec3 skyTop = mix(nightTop, dayTop, dayFactor);
    vec3 skyHorizon = mix(nightHorizon, dayHorizon, dayFactor);
    vec3 skyColor = mix(skyHorizon, skyTop, horizon);
    
    // Add sun and glow contribution to sky
    skyColor += sunDisk * sunColor * 5.0;  // Bright sun disk
    skyColor += corona * coronaColor;       // Soft corona
    skyColor += glare * glareColor * (1.0 - horizon);  // Glare fades at horizon
    
    // Add subtle stars at top of sky
    if (dir.y > 0.2) {
        float starIntensity = pow(dir.y - 0.2, 2.0);
        float stars = step(0.998, hash(floor(dir.xz * 500.0)));
        stars *= hash(floor(dir.xz * 500.0 + 0.5)) * 0.5 + 0.5;
        skyColor += vec3(stars * starIntensity * 0.8);
    }
    
    // Mountains on the horizon
    float angle = atan(dir.x, dir.z);  // Horizontal angle around Y axis
    vec2 mountainCoord = vec2(angle * 3.0, 0.0);
    
    // Multiple mountain layers for depth
    // Far mountains (blue-gray, misty)
    float mountain1 = fbm(mountainCoord * 1.5 + vec2(0.0, 100.0)) * 0.15 + 0.02;
    // Mid mountains (darker)
    float mountain2 = fbm(mountainCoord * 2.5 + vec2(50.0, 0.0)) * 0.12 + 0.01;
    // Near mountains/hills (darkest)
    float mountain3 = fbm(mountainCoord * 4.0 + vec2(25.0, 50.0)) * 0.08 + 0.005;
    
    // Draw mountains based on vertical direction
    float verticalPos = dir.y;
    
    // Far mountains
    if (verticalPos < mountain1 && verticalPos > -0.1) {
        float fogAmount = smoothstep(0.0, mountain1, verticalPos);
        vec3 mountainColor = vec3(0.12, 0.15, 0.22);  // Blue-gray
        // Snow caps on far mountains
        float snowLine = mountain1 - 0.03;
        if (verticalPos > snowLine) {
            float snow = smoothstep(snowLine, snowLine + 0.02, verticalPos);
            mountainColor = mix(mountainColor, vec3(0.4, 0.45, 0.5), snow * 0.6);
        }
        skyColor = mix(mountainColor, skyColor, fogAmount * 0.7);
    }
    
    // Mid mountains
    if (verticalPos < mountain2 && verticalPos > -0.1) {
        float fogAmount = smoothstep(-0.02, mountain2, verticalPos);
        vec3 mountainColor = vec3(0.08, 0.1, 0.15);  // Darker blue
        // Snow on mid peaks
        float snowLine = mountain2 - 0.02;
        if (verticalPos > snowLine) {
            float snow = smoothstep(snowLine, snowLine + 0.015, verticalPos);
            mountainColor = mix(mountainColor, vec3(0.3, 0.35, 0.4), snow * 0.5);
        }
        skyColor = mix(mountainColor, skyColor, fogAmount * 0.5);
    }
    
    // Near hills/mountains
    if (verticalPos < mountain3 && verticalPos > -0.1) {
        float fogAmount = smoothstep(-0.03, mountain3, verticalPos);
        vec3 mountainColor = vec3(0.05, 0.07, 0.1);  // Very dark
        skyColor = mix(mountainColor, skyColor, fogAmount * 0.3);
    }
    
    // Subtle fog/mist at horizon
    float fog = exp(-abs(dir.y) * 8.0);
    vec3 fogColor = vec3(0.12, 0.14, 0.2);
    skyColor = mix(skyColor, fogColor, fog * 0.4);
    
    // Very subtle moving clouds (optional atmospheric effect)
    float cloudNoise = fbm(vec2(angle * 2.0 + time * 0.01, dir.y * 5.0));
    if (dir.y > 0.1 && dir.y < 0.5) {
        float cloudMask = smoothstep(0.1, 0.2, dir.y) * smoothstep(0.5, 0.3, dir.y);
        float clouds = smoothstep(0.4, 0.6, cloudNoise) * cloudMask * 0.15;
        skyColor += vec3(clouds);
    }
    
    FragColor = vec4(skyColor, 1.0);
}
)";

// 3D Mountains with distance fog
const char* const mountains_vertex = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in float aHeight;

out vec3 FragPos;
out vec3 Normal;
out float Height;
out float Distance;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform vec3 cameraPos;

void main() {
    vec4 worldPos = model * vec4(aPos, 1.0);
    FragPos = worldPos.xyz;
    Normal = mat3(transpose(inverse(model))) * aNormal;
    Height = aHeight;
    Distance = length(worldPos.xyz - cameraPos);
    gl_Position = projection * view * worldPos;
}
)";

const char* const mountains_fragment = R"(
#version 330 core
out vec4 FragColor;

in vec3 FragPos;
in vec3 Normal;
in float Height;
in float Distance;

uniform vec3 fogColor;
uniform float fogDensity;
uniform float fogStart;

void main() {
    // Base mountain color based on height
    vec3 baseColor;
    float h = Height;
    
    // Rock at base, lighter rock mid, snow at peaks
    vec3 darkRock = vec3(0.15, 0.13, 0.12);
    vec3 midRock = vec3(0.25, 0.23, 0.22);
    vec3 lightRock = vec3(0.35, 0.33, 0.32);
    vec3 snow = vec3(0.85, 0.88, 0.92);
    
    if (h < 0.3) {
        baseColor = mix(darkRock, midRock, h / 0.3);
    } else if (h < 0.6) {
        baseColor = mix(midRock, lightRock, (h - 0.3) / 0.3);
    } else if (h < 0.75) {
        baseColor = mix(lightRock, snow, (h - 0.6) / 0.15);
    } else {
        baseColor = snow;
    }
    
    // Simple directional lighting
    vec3 lightDir = normalize(vec3(-0.3, -1.0, -0.5));
    vec3 norm = normalize(Normal);
    float diff = max(dot(norm, -lightDir), 0.0) * 0.6 + 0.4;
    
    vec3 litColor = baseColor * diff;
    
    // Atmospheric fog based on distance
    float fogFactor = 1.0 - exp(-fogDensity * max(0.0, Distance - fogStart));
    fogFactor = clamp(fogFactor, 0.0, 0.95);
    
    // Add subtle blue tint to distant mountains (atmospheric scattering)
    vec3 atmosphereColor = mix(fogColor, vec3(0.4, 0.5, 0.7), 0.3);
    vec3 finalColor = mix(litColor, atmosphereColor, fogFactor);
    
    FragColor = vec4(finalColor, 1.0);
}
)";

// Skinned model vertex shader with skeletal animation support
const char* const skinned_model_vertex = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoord;
layout (location = 3) in vec4 aColor;
layout (location = 4) in ivec4 aJoints;
layout (location = 5) in vec4 aWeights;

out vec3 FragPos;
out vec3 Normal;
out vec2 TexCoord;
out vec4 VertexColor;
out float FogDistance;
out vec4 FragPosLightSpace;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform vec3 cameraPos;
uniform mat4 lightSpaceMatrix;

const int MAX_BONES = 64;
uniform mat4 boneMatrices[MAX_BONES];
uniform int useSkinning;

void main() {
    vec4 localPos = vec4(aPos, 1.0);
    vec4 localNormal = vec4(aNormal, 0.0);
    
    if (useSkinning == 1) {
        // Apply skeletal animation
        mat4 skinMatrix = 
            aWeights.x * boneMatrices[aJoints.x] +
            aWeights.y * boneMatrices[aJoints.y] +
            aWeights.z * boneMatrices[aJoints.z] +
            aWeights.w * boneMatrices[aJoints.w];
        
        localPos = skinMatrix * vec4(aPos, 1.0);
        localNormal = skinMatrix * vec4(aNormal, 0.0);
    }
    
    vec4 worldPos = model * localPos;
    FragPos = worldPos.xyz;
    Normal = mat3(transpose(inverse(model))) * localNormal.xyz;
    TexCoord = aTexCoord;
    VertexColor = aColor;
    FogDistance = length(worldPos.xyz - cameraPos);
    FragPosLightSpace = lightSpaceMatrix * worldPos;
    gl_Position = projection * view * worldPos;
}
)";

// Skinned model fragment shader (same as regular model with fog and shadows)
const char* const skinned_model_fragment = R"(
#version 330 core
out vec4 FragColor;

in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoord;
in vec4 VertexColor;
in float FogDistance;
in vec4 FragPosLightSpace;

uniform vec3 lightDir;
uniform vec3 lightColor;
uniform vec3 ambientColor;
uniform vec4 tintColor;
uniform sampler2D baseColorTexture;
uniform int hasTexture;

// Shadow mapping
uniform sampler2D shadowMap;
uniform int shadowsEnabled;

// SSAO
uniform sampler2D ssaoTexture;
uniform int ssaoEnabled;
uniform vec2 screenSize;

// Fog uniforms
uniform vec3 fogColor;
uniform float fogStart;
uniform float fogEnd;
uniform int fogEnabled;

// Calculate shadow with PCF soft shadows
float calculateShadow(vec4 fragPosLightSpace, vec3 normal, vec3 lightDirection) {
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords = projCoords * 0.5 + 0.5;
    
    if (projCoords.z > 1.0 || projCoords.x < 0.0 || projCoords.x > 1.0 || 
        projCoords.y < 0.0 || projCoords.y > 1.0) {
        return 0.0;
    }
    
    float currentDepth = projCoords.z;
    float bias = max(0.005 * (1.0 - dot(normal, lightDirection)), 0.001);
    
    float shadow = 0.0;
    vec2 texelSize = 1.0 / textureSize(shadowMap, 0);
    for (int x = -2; x <= 2; ++x) {
        for (int y = -2; y <= 2; ++y) {
            float pcfDepth = texture(shadowMap, projCoords.xy + vec2(x, y) * texelSize).r;
            shadow += currentDepth - bias > pcfDepth ? 1.0 : 0.0;
        }
    }
    shadow /= 25.0;
    
    return shadow;
}

void main() {
    vec3 norm = normalize(Normal);
    vec3 lightDirection = normalize(-lightDir);
    
    float shadow = 0.0;
    if (shadowsEnabled == 1) {
        shadow = calculateShadow(FragPosLightSpace, norm, lightDirection);
    }
    
    float diff = max(dot(norm, lightDirection), 0.0);
    vec3 diffuse = diff * lightColor * (1.0 - shadow * 0.7);
    
    float ao = 1.0;
    if (ssaoEnabled == 1) {
        vec2 screenUV = gl_FragCoord.xy / screenSize;
        ao = texture(ssaoTexture, screenUV).r;
    }
    
    vec3 ambient = ambientColor * ao;
    vec3 lighting = ambient + diffuse;
    
    vec4 baseColor;
    if (hasTexture == 1) {
        baseColor = texture(baseColorTexture, TexCoord);
    } else {
        baseColor = VertexColor * tintColor;
    }
    
    vec3 result = lighting * baseColor.rgb;
    
    vec3 viewDir = normalize(-FragPos);
    float rim = 1.0 - max(dot(viewDir, norm), 0.0);
    rim = smoothstep(0.6, 1.0, rim);
    result += rim * 0.3 * baseColor.rgb;
    
    if (fogEnabled == 1) {
        float fogFactor = clamp((FogDistance - fogStart) / (fogEnd - fogStart), 0.0, 1.0);
        fogFactor = 1.0 - exp(-fogFactor * 2.0);
        result = mix(result, fogColor, fogFactor);
    }
    
    FragColor = vec4(result, baseColor.a);
}
)";

// Shadow depth pass vertex shader (for rendering shadow map)
const char* const shadow_depth_vertex = R"(
#version 330 core
layout (location = 0) in vec3 aPos;

uniform mat4 lightSpaceMatrix;
uniform mat4 model;

void main() {
    gl_Position = lightSpaceMatrix * model * vec4(aPos, 1.0);
}
)";

// Shadow depth pass fragment shader
const char* const shadow_depth_fragment = R"(
#version 330 core

void main() {
    // Depth is written automatically
    // gl_FragDepth = gl_FragCoord.z; // implicit
}
)";

// Skinned shadow depth vertex shader (for animated models)
const char* const skinned_shadow_depth_vertex = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 4) in ivec4 aJoints;
layout (location = 5) in vec4 aWeights;

uniform mat4 lightSpaceMatrix;
uniform mat4 model;

const int MAX_BONES = 64;
uniform mat4 boneMatrices[MAX_BONES];
uniform int useSkinning;

void main() {
    vec4 localPos = vec4(aPos, 1.0);
    
    if (useSkinning == 1) {
        mat4 skinMatrix = 
            aWeights.x * boneMatrices[aJoints.x] +
            aWeights.y * boneMatrices[aJoints.y] +
            aWeights.z * boneMatrices[aJoints.z] +
            aWeights.w * boneMatrices[aJoints.w];
        
        localPos = skinMatrix * vec4(aPos, 1.0);
    }
    
    gl_Position = lightSpaceMatrix * model * localPos;
}
)";

// SSAO G-buffer vertex shader (outputs position and normal to textures)
const char* const ssao_gbuffer_vertex = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;

out vec3 FragPos;
out vec3 Normal;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main() {
    vec4 viewPos = view * model * vec4(aPos, 1.0);
    FragPos = viewPos.xyz;
    
    mat3 normalMatrix = transpose(inverse(mat3(view * model)));
    Normal = normalMatrix * aNormal;
    
    gl_Position = projection * viewPos;
}
)";

// SSAO G-buffer fragment shader
const char* const ssao_gbuffer_fragment = R"(
#version 330 core
layout (location = 0) out vec3 gPosition;
layout (location = 1) out vec3 gNormal;

in vec3 FragPos;
in vec3 Normal;

void main() {
    gPosition = FragPos;
    gNormal = normalize(Normal);
}
)";

// SSAO calculation shader (screen-space pass)
const char* const ssao_vertex = R"(
#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aTexCoords;

out vec2 TexCoords;

void main() {
    TexCoords = aTexCoords;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

const char* const ssao_fragment = R"(
#version 330 core
out float FragColor;

in vec2 TexCoords;

uniform sampler2D gPosition;
uniform sampler2D gNormal;
uniform sampler2D texNoise;

uniform vec3 samples[64];
uniform mat4 projection;

uniform vec2 screenSize;
uniform float radius;
uniform float bias;

const int kernelSize = 32;

void main() {
    vec2 noiseScale = screenSize / 4.0;
    
    vec3 fragPos = texture(gPosition, TexCoords).xyz;
    vec3 normal = normalize(texture(gNormal, TexCoords).rgb);
    vec3 randomVec = normalize(texture(texNoise, TexCoords * noiseScale).xyz);
    
    // If position is at far plane or invalid, no occlusion
    if (length(fragPos) < 0.1 || length(fragPos) > 1000.0) {
        FragColor = 1.0;
        return;
    }
    
    // Create TBN change-of-basis matrix
    vec3 tangent = normalize(randomVec - normal * dot(randomVec, normal));
    vec3 bitangent = cross(normal, tangent);
    mat3 TBN = mat3(tangent, bitangent, normal);
    
    float occlusion = 0.0;
    for (int i = 0; i < kernelSize; ++i) {
        // Get sample position in view space
        vec3 samplePos = TBN * samples[i];
        samplePos = fragPos + samplePos * radius;
        
        // Project sample position to screen space
        vec4 offset = vec4(samplePos, 1.0);
        offset = projection * offset;
        offset.xyz /= offset.w;
        offset.xyz = offset.xyz * 0.5 + 0.5;
        
        // Get sample depth
        float sampleDepth = texture(gPosition, offset.xy).z;
        
        // Range check and accumulate
        float rangeCheck = smoothstep(0.0, 1.0, radius / abs(fragPos.z - sampleDepth));
        occlusion += (sampleDepth >= samplePos.z + bias ? 1.0 : 0.0) * rangeCheck;
    }
    
    occlusion = 1.0 - (occlusion / float(kernelSize));
    FragColor = pow(occlusion, 2.0);  // Increase contrast
}
)";

// SSAO blur shader (removes noise)
const char* const ssao_blur_fragment = R"(
#version 330 core
out float FragColor;

in vec2 TexCoords;

uniform sampler2D ssaoInput;

void main() {
    vec2 texelSize = 1.0 / vec2(textureSize(ssaoInput, 0));
    float result = 0.0;
    
    for (int x = -2; x <= 2; ++x) {
        for (int y = -2; y <= 2; ++y) {
            vec2 offset = vec2(float(x), float(y)) * texelSize;
            result += texture(ssaoInput, TexCoords + offset).r;
        }
    }
    
    FragColor = result / 25.0;
}
)";

// Text rendering shader
const char* const text_vertex = R"(
#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aTexCoord;

out vec2 TexCoord;

uniform mat4 projection;

void main() {
    TexCoord = aTexCoord;
    gl_Position = projection * vec4(aPos, 0.0, 1.0);
}
)";

const char* const text_fragment = R"(
#version 330 core
out vec4 FragColor;

in vec2 TexCoord;

uniform sampler2D textTexture;
uniform vec4 textColor;

void main() {
    vec4 sampled = texture(textTexture, TexCoord);
    FragColor = textColor * sampled;
}
)";

} // namespace shaders

} // namespace mmo
