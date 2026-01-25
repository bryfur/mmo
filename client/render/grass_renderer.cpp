#include "grass_renderer.hpp"
#include <iostream>
#include <cmath>
#include <vector>

namespace mmo {

// Vertex shader - just passes grid index to geometry shader
// The geometry shader computes actual world position based on camera
const char* const grass_vertex_shader = R"(
#version 330 core

layout (location = 0) in vec2 gridOffset;  // Offset from camera in grid units

out vec2 vGridOffset;

void main() {
    vGridOffset = gridOffset;
    gl_Position = vec4(0.0, 0.0, 0.0, 1.0);  // Unused, GS computes position
}
)";

// Geometry shader - generates grass blades procedurally based on world position
const char* const grass_geometry_shader = R"(
#version 330 core

layout (points) in;
layout (triangle_strip, max_vertices = 12) out;

in vec2 vGridOffset[];

out vec3 FragPos;
out vec3 Normal;
out vec2 UV;
out vec4 FragPosLightSpace;

uniform mat4 view;
uniform mat4 projection;
uniform mat4 lightSpaceMatrix;
uniform vec3 cameraPos;
uniform float time;
uniform float windMagnitude;
uniform float windWaveLength;
uniform float windWavePeriod;
uniform float grassSpacing;
uniform float viewDistance;

// World parameters
uniform float worldWidth;   // Total world width (e.g., 8000)
uniform float worldHeight;  // Total world height (e.g., 8000)

// Heightmap texture from server
uniform sampler2D heightmapTexture;
uniform int hasHeightmap;  // 1 if heightmap is available

// Height range constants (must match heightmap_config in C++)
const float MIN_HEIGHT = -500.0;
const float MAX_HEIGHT = 500.0;

// Hash functions for procedural generation
float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

vec2 hash2(vec2 p) {
    return fract(sin(vec2(dot(p, vec2(127.1, 311.7)), dot(p, vec2(269.5, 183.3)))) * 43758.5453);
}

// Sample terrain height - use same procedural formula as server heightmap
float getTerrainHeight(float world_x, float world_z) {
    float world_center_x = worldWidth / 2.0;
    float world_center_z = worldHeight / 2.0;
    
    float dx = world_x - world_center_x;
    float dz = world_z - world_center_z;
    float dist = sqrt(dx * dx + dz * dz);
    
    // Keep playable area relatively flat
    float playable_radius = 600.0;
    float transition_radius = 400.0;
    float flatness = 1.0;
    
    if (dist < playable_radius) {
        flatness = 0.1;
    } else if (dist < playable_radius + transition_radius) {
        float t = (dist - playable_radius) / transition_radius;
        flatness = 0.1 + t * 0.9;
    }
    
    // Multi-octave noise for natural terrain
    float height = 0.0;
    
    // Large rolling hills
    float freq1 = 0.0008;
    height += sin(world_x * freq1 * 1.1) * cos(world_z * freq1 * 0.9) * 80.0;
    height += sin(world_x * freq1 * 0.7 + 1.3) * sin(world_z * freq1 * 1.2 + 0.7) * 60.0;
    
    // Medium undulations
    float freq2 = 0.003;
    height += sin(world_x * freq2 * 1.3 + 2.1) * cos(world_z * freq2 * 0.8 + 1.4) * 25.0;
    height += cos(world_x * freq2 * 0.9) * sin(world_z * freq2 * 1.1 + 0.5) * 20.0;
    
    // Small bumps
    float freq3 = 0.01;
    height += sin(world_x * freq3 * 1.7 + 0.3) * cos(world_z * freq3 * 1.4 + 2.1) * 8.0;
    height += cos(world_x * freq3 * 1.2 + 1.8) * sin(world_z * freq3 * 0.9) * 6.0;
    
    height *= flatness;
    
    // Terrain rises toward mountains at edges
    if (dist > 2000.0) {
        float rise_factor = (dist - 2000.0) / 2000.0;
        rise_factor = min(rise_factor, 1.0);
        height += rise_factor * rise_factor * 150.0;
    }
    
    return height;
}

void main() {
    // Compute world position: snap camera to grid, then add offset
    vec2 cameraGrid = floor(cameraPos.xz / grassSpacing) * grassSpacing;
    vec2 worldPos2D = cameraGrid + vGridOffset[0] * grassSpacing;
    
    // World bounds check
    float margin = 50.0;
    if (worldPos2D.x < margin || worldPos2D.x > worldWidth - margin ||
        worldPos2D.y < margin || worldPos2D.y > worldHeight - margin) {
        return;
    }
    
    // Skip town center area
    vec2 townCenter = vec2(worldWidth * 0.5, worldHeight * 0.5);
    if (abs(worldPos2D.x - townCenter.x) < 200.0 && abs(worldPos2D.y - townCenter.y) < 200.0) {
        return;
    }
    
    // Use world position as seed for consistent grass properties
    vec2 seed = floor(worldPos2D / grassSpacing);
    
    // Jitter position within cell for natural look
    vec2 jitter = (hash2(seed) - 0.5) * grassSpacing * 0.8;
    float grassX = worldPos2D.x + jitter.x;
    float grassZ = worldPos2D.y + jitter.y;
    float terrainY = getTerrainHeight(grassX, grassZ);
    vec3 basePos = vec3(grassX, terrainY, grassZ);
    
    // Distance culling with soft fade
    float dist = length(basePos.xz - cameraPos.xz);
    if (dist > viewDistance) return;
    
    // Random grass properties from hash
    float h1 = hash(seed);
    float h2 = hash(seed + vec2(1.0, 0.0));
    float h3 = hash(seed + vec2(0.0, 1.0));
    float h4 = hash(seed + vec2(1.0, 1.0));
    float h5 = hash(seed + vec2(3.0, 3.0));
    
    float orientation = h1 * 3.14159;
    // More varied height: mix of short and tall grass
    float heightMult = h5 * h5;  // Squared for more short grass, fewer tall
    float bladeHeight = 3.0 + h2 * 6.0 + heightMult * 8.0;  // 3-17 units, skewed shorter
    float bladeWidth = 0.8 + h3 * 0.6;     // 0.8-1.4 units wide
    float stiffness = 0.3 + h4 * 0.4;       // 0.3-0.7
    
    // Compute terrain normal for slope alignment
    float eps = 2.0;
    float hL = getTerrainHeight(grassX - eps, grassZ);
    float hR = getTerrainHeight(grassX + eps, grassZ);
    float hD = getTerrainHeight(grassX, grassZ - eps);
    float hU = getTerrainHeight(grassX, grassZ + eps);
    vec3 terrainNormal = normalize(vec3(hL - hR, 2.0 * eps, hD - hU));
    
    // Tilt for natural look, combined with terrain slope
    float tiltX = (hash(seed + vec2(2.0, 0.0)) - 0.5) * 0.4;
    float tiltZ = (hash(seed + vec2(0.0, 2.0)) - 0.5) * 0.4;
    vec3 up = normalize(terrainNormal + vec3(tiltX, 0.0, tiltZ));
    
    // Width direction perpendicular to up
    vec3 widthDir = vec3(cos(orientation), 0.0, sin(orientation));
    widthDir = normalize(cross(up, widthDir));
    
    // Wind displacement
    float windPhase = time / windWavePeriod;
    vec3 windForce = windMagnitude * vec3(
        sin(windPhase + basePos.x * 0.1 / windWaveLength),
        0.0,
        sin(windPhase + basePos.z * 0.15 / windWaveLength) * 0.3
    );
    vec3 windOffset = windForce * (1.0 - stiffness) * bladeHeight * 0.3;
    
    // Bezier control points
    vec3 p0 = basePos;
    vec3 p1 = basePos + up * bladeHeight * 0.5;
    vec3 p2 = basePos + up * bladeHeight + windOffset;
    
    // LOD: fewer segments at distance
    int segments = dist < 150.0 ? 5 : (dist < 350.0 ? 3 : 2);
    float segmentStep = 1.0 / float(segments);
    
    // Face normal for lighting
    vec3 toCamera = normalize(cameraPos - basePos);
    vec3 bladeNormal = normalize(cross(up, widthDir));
    bladeNormal = normalize(bladeNormal + toCamera * 0.3);
    
    // Generate triangle strip along blade
    float baseWidth = bladeWidth * 0.5;
    
    for (int i = 0; i <= segments; i++) {
        float t = float(i) * segmentStep;
        
        // Quadratic Bezier
        float t1 = 1.0 - t;
        vec3 pos = t1 * t1 * p0 + 2.0 * t1 * t * p1 + t * t * p2;
        
        // Taper width toward tip
        float w = baseWidth * (1.0 - t * 0.9);
        
        // Left vertex
        vec3 leftPos = pos - widthDir * w;
        gl_Position = projection * view * vec4(leftPos, 1.0);
        FragPos = leftPos;
        Normal = bladeNormal;
        UV = vec2(0.0, t);
        FragPosLightSpace = lightSpaceMatrix * vec4(leftPos, 1.0);
        EmitVertex();
        
        // Right vertex
        vec3 rightPos = pos + widthDir * w;
        gl_Position = projection * view * vec4(rightPos, 1.0);
        FragPos = rightPos;
        Normal = bladeNormal;
        UV = vec2(1.0, t);
        FragPosLightSpace = lightSpaceMatrix * vec4(rightPos, 1.0);
        EmitVertex();
    }
    
    EndPrimitive();
}
)";

// Fragment shader - lighting and color with shadows
const char* const grass_fragment_shader = R"(
#version 330 core

in vec3 FragPos;
in vec3 Normal;
in vec2 UV;
in vec4 FragPosLightSpace;

out vec4 FragColor;

uniform vec3 cameraPos;
uniform vec3 fogColor;
uniform float fogStart;
uniform float fogEnd;
uniform vec3 lightDir;

// Shadow mapping
uniform sampler2D shadowMap;
uniform int shadowsEnabled;

// Calculate shadow with PCF soft shadows
float calculateShadow(vec4 fragPosLightSpace, vec3 normal) {
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords = projCoords * 0.5 + 0.5;
    
    if (projCoords.z > 1.0 || projCoords.x < 0.0 || projCoords.x > 1.0 || 
        projCoords.y < 0.0 || projCoords.y > 1.0) {
        return 0.0;
    }
    
    float currentDepth = projCoords.z;
    float bias = 0.003;  // Slightly larger bias for grass to prevent self-shadowing artifacts
    
    float shadow = 0.0;
    vec2 texelSize = 1.0 / textureSize(shadowMap, 0);
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            float pcfDepth = texture(shadowMap, projCoords.xy + vec2(x, y) * texelSize).r;
            shadow += currentDepth - bias > pcfDepth ? 1.0 : 0.0;
        }
    }
    shadow /= 9.0;
    
    return shadow;
}

void main() {
    // Color gradient: dark at base, bright at tip
    vec3 baseColor = vec3(0.08, 0.18, 0.04);
    vec3 tipColor = vec3(0.25, 0.55, 0.12);
    vec3 grassColor = mix(baseColor, tipColor, UV.y);
    
    // Simple lighting
    vec3 norm = normalize(Normal);
    vec3 lightDirection = normalize(-lightDir);
    
    // Calculate shadow
    float shadow = 0.0;
    if (shadowsEnabled == 1) {
        shadow = calculateShadow(FragPosLightSpace, norm);
    }
    
    float ambient = 0.4;
    float diff = max(abs(dot(norm, lightDirection)), 0.0) * 0.5 * (1.0 - shadow * 0.6);
    
    // Subtle specular at tips
    vec3 viewDir = normalize(cameraPos - FragPos);
    vec3 halfDir = normalize(lightDirection + viewDir);
    float spec = pow(max(dot(norm, halfDir), 0.0), 16.0) * UV.y * 0.2 * (1.0 - shadow);
    
    vec3 result = grassColor * (ambient + diff) + vec3(spec);
    
    // Distance fog
    float dist = length(FragPos - cameraPos);
    float fogFactor = clamp((dist - fogStart) / (fogEnd - fogStart), 0.0, 1.0);
    fogFactor = 1.0 - exp(-fogFactor * 2.0);
    result = mix(result, fogColor, fogFactor);
    
    FragColor = vec4(result, 1.0);
}
)";

GrassRenderer::GrassRenderer() = default;

GrassRenderer::~GrassRenderer() {
    shutdown();
}

void GrassRenderer::init(float world_width, float world_height) {
    if (initialized_) return;
    
    std::cout << "Initializing grass renderer (GPU procedural)..." << std::endl;
    
    world_width_ = world_width;
    world_height_ = world_height;
    
    load_shaders();
    create_grid_mesh();
    
    initialized_ = true;
    std::cout << "Grass renderer initialized with " << vertex_count_ << " potential grass points" << std::endl;
}

void GrassRenderer::load_shaders() {
    // Compile vertex shader
    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    const char* vs_src = grass_vertex_shader;
    glShaderSource(vs, 1, &vs_src, nullptr);
    glCompileShader(vs);
    
    GLint success;
    glGetShaderiv(vs, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[512];
        glGetShaderInfoLog(vs, 512, nullptr, log);
        std::cerr << "Grass vertex shader error: " << log << std::endl;
    }
    
    // Compile geometry shader
    GLuint gs = glCreateShader(GL_GEOMETRY_SHADER);
    const char* gs_src = grass_geometry_shader;
    glShaderSource(gs, 1, &gs_src, nullptr);
    glCompileShader(gs);
    
    glGetShaderiv(gs, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[512];
        glGetShaderInfoLog(gs, 512, nullptr, log);
        std::cerr << "Grass geometry shader error: " << log << std::endl;
    }
    
    // Compile fragment shader
    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    const char* fs_src = grass_fragment_shader;
    glShaderSource(fs, 1, &fs_src, nullptr);
    glCompileShader(fs);
    
    glGetShaderiv(fs, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[512];
        glGetShaderInfoLog(fs, 512, nullptr, log);
        std::cerr << "Grass fragment shader error: " << log << std::endl;
    }
    
    // Link program
    grass_program_ = glCreateProgram();
    glAttachShader(grass_program_, vs);
    glAttachShader(grass_program_, gs);
    glAttachShader(grass_program_, fs);
    glLinkProgram(grass_program_);
    
    glGetProgramiv(grass_program_, GL_LINK_STATUS, &success);
    if (!success) {
        char log[512];
        glGetProgramInfoLog(grass_program_, 512, nullptr, log);
        std::cerr << "Grass shader link error: " << log << std::endl;
    }
    
    glDeleteShader(vs);
    glDeleteShader(gs);
    glDeleteShader(fs);
}

void GrassRenderer::create_grid_mesh() {
    // Create a grid of offsets centered at origin
    // Each point represents a potential grass blade position relative to camera
    grid_size_ = static_cast<int>(grass_view_distance * 2.0f / grass_spacing);
    
    std::vector<glm::vec2> offsets;
    offsets.reserve(grid_size_ * grid_size_);
    
    int half = grid_size_ / 2;
    for (int z = -half; z <= half; ++z) {
        for (int x = -half; x <= half; ++x) {
            offsets.emplace_back(static_cast<float>(x), static_cast<float>(z));
        }
    }
    
    vertex_count_ = static_cast<int>(offsets.size());
    
    glGenVertexArrays(1, &grass_vao_);
    glGenBuffers(1, &grass_vbo_);
    
    glBindVertexArray(grass_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, grass_vbo_);
    glBufferData(GL_ARRAY_BUFFER, offsets.size() * sizeof(glm::vec2), offsets.data(), GL_STATIC_DRAW);
    
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(glm::vec2), nullptr);
    glEnableVertexAttribArray(0);
    
    glBindVertexArray(0);
}

void GrassRenderer::update(float delta_time, float current_time) {
    current_time_ = current_time;
}

void GrassRenderer::render(const glm::mat4& view, const glm::mat4& projection, const glm::vec3& camera_pos,
                           const glm::mat4& light_space_matrix, GLuint shadow_map, bool shadows_enabled,
                           const glm::vec3& light_dir) {
    if (!initialized_ || vertex_count_ == 0) return;
    
    glUseProgram(grass_program_);
    
    // Set uniforms
    glUniformMatrix4fv(glGetUniformLocation(grass_program_, "view"), 1, GL_FALSE, &view[0][0]);
    glUniformMatrix4fv(glGetUniformLocation(grass_program_, "projection"), 1, GL_FALSE, &projection[0][0]);
    glUniformMatrix4fv(glGetUniformLocation(grass_program_, "lightSpaceMatrix"), 1, GL_FALSE, &light_space_matrix[0][0]);
    glUniform3fv(glGetUniformLocation(grass_program_, "cameraPos"), 1, &camera_pos[0]);
    glUniform3fv(glGetUniformLocation(grass_program_, "lightDir"), 1, &light_dir[0]);
    glUniform1f(glGetUniformLocation(grass_program_, "time"), current_time_);
    glUniform1f(glGetUniformLocation(grass_program_, "windMagnitude"), wind_magnitude);
    glUniform1f(glGetUniformLocation(grass_program_, "windWaveLength"), wind_wave_length);
    glUniform1f(glGetUniformLocation(grass_program_, "windWavePeriod"), wind_wave_period);
    glUniform1f(glGetUniformLocation(grass_program_, "grassSpacing"), grass_spacing);
    glUniform1f(glGetUniformLocation(grass_program_, "viewDistance"), grass_view_distance);
    
    // World dimensions for height calculation
    glUniform1f(glGetUniformLocation(grass_program_, "worldWidth"), world_width_);
    glUniform1f(glGetUniformLocation(grass_program_, "worldHeight"), world_height_);
    
    // Shadow mapping (texture unit 0)
    glUniform1i(glGetUniformLocation(grass_program_, "shadowsEnabled"), shadows_enabled ? 1 : 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, shadow_map);
    glUniform1i(glGetUniformLocation(grass_program_, "shadowMap"), 0);
    
    // Heightmap texture (texture unit 1)
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, heightmap_texture_);
    glUniform1i(glGetUniformLocation(grass_program_, "heightmapTexture"), 1);
    glUniform1i(glGetUniformLocation(grass_program_, "hasHeightmap"), heightmap_texture_ != 0 ? 1 : 0);
    
    // Fog
    glUniform3f(glGetUniformLocation(grass_program_, "fogColor"), 0.12f, 0.14f, 0.2f);
    glUniform1f(glGetUniformLocation(grass_program_, "fogStart"), 300.0f);
    glUniform1f(glGetUniformLocation(grass_program_, "fogEnd"), grass_view_distance);
    
    glDisable(GL_CULL_FACE);
    
    glBindVertexArray(grass_vao_);
    glDrawArrays(GL_POINTS, 0, vertex_count_);
    glBindVertexArray(0);
    
    glEnable(GL_CULL_FACE);
}

void GrassRenderer::shutdown() {
    if (grass_vao_) {
        glDeleteVertexArrays(1, &grass_vao_);
        grass_vao_ = 0;
    }
    if (grass_vbo_) {
        glDeleteBuffers(1, &grass_vbo_);
        grass_vbo_ = 0;
    }
    if (grass_program_) {
        glDeleteProgram(grass_program_);
        grass_program_ = 0;
    }
    initialized_ = false;
}

} // namespace mmo
