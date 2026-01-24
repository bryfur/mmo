#include "grass_renderer.hpp"
#include "bgfx_utils.hpp"
#include <iostream>
#include <cmath>
#include <vector>
#include <glm/gtc/type_ptr.hpp>

namespace mmo {

namespace {

// Simple hash for consistent random positioning
float hash(float x, float y) {
    return std::fmod(std::sin(x * 127.1f + y * 311.7f) * 43758.5453f, 1.0f);
}

// Grass blade vertex structure
struct GrassVertex {
    float x, y, z;    // Position (local to blade)
    float u, v;       // UV coords
    float nx, ny, nz; // Normal
};

// Grass instance data - one per grass blade
struct GrassInstance {
    float x, z;       // World position XZ
    float rotation;   // Y-axis rotation
    float scale;      // Height scale
    float seed;       // Random seed for wind variation
};

} // anonymous namespace

GrassRenderer::GrassRenderer() = default;

GrassRenderer::~GrassRenderer() {
    shutdown();
}

void GrassRenderer::init(float world_width, float world_height) {
    if (initialized_) return;
    
    std::cout << "Initializing grass renderer (bgfx instanced)..." << std::endl;
    
    world_width_ = world_width;
    world_height_ = world_height;
    
    // Note: u_viewProj is a bgfx predefined uniform - use setViewTransform
    // Create only custom uniforms
    u_cameraPos_ = bgfx::createUniform("u_cameraPos", bgfx::UniformType::Vec4);
    u_lightDir_ = bgfx::createUniform("u_lightDir", bgfx::UniformType::Vec4);
    u_grassParams_ = bgfx::createUniform("u_grassParams", bgfx::UniformType::Vec4);
    u_fogParams_ = bgfx::createUniform("u_fogParams", bgfx::UniformType::Vec4);
    u_fogParams2_ = bgfx::createUniform("u_fogParams2", bgfx::UniformType::Vec4);
    u_worldBounds_ = bgfx::createUniform("u_worldBounds", bgfx::UniformType::Vec4);
    u_lightSpaceMatrix_ = bgfx::createUniform("u_lightSpaceMatrix", bgfx::UniformType::Mat4);
    s_shadowMap_ = bgfx::createUniform("s_shadowMap", bgfx::UniformType::Sampler);
    
    load_shaders();
    create_blade_mesh();
    create_instance_buffer();
    
    initialized_ = true;
    std::cout << "Grass renderer initialized with " << instance_count_ << " grass blades" << std::endl;
}

void GrassRenderer::load_shaders() {
    program_ = bgfx_utils::load_program("grass_vs", "grass_fs");
    if (!bgfx::isValid(program_)) {
        std::cerr << "Failed to load grass shaders" << std::endl;
    }
}

void GrassRenderer::create_blade_mesh() {
    // Create a single grass blade mesh - a tapered quad
    // Vertices form a blade shape that will be instanced
    
    blade_layout_
        .begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
        .end();
    
    // Blade is centered at origin, growing up along Y axis
    // Width tapers from bottom to top
    const float width = 0.5f;
    const float height = 1.0f;  // Normalized, will be scaled by instance
    const int segments = 3;
    
    std::vector<GrassVertex> vertices;
    std::vector<uint16_t> indices;
    
    // Create blade segments
    for (int i = 0; i <= segments; ++i) {
        float t = static_cast<float>(i) / segments;
        float y = t * height;
        float w = width * (1.0f - t * 0.9f);  // Taper toward tip
        
        // Left vertex
        vertices.push_back({-w, y, 0.0f, 0.0f, t, 0.0f, 0.0f, 1.0f});
        // Right vertex
        vertices.push_back({w, y, 0.0f, 1.0f, t, 0.0f, 0.0f, 1.0f});
    }
    
    // Create indices for triangle strip as triangles
    for (int i = 0; i < segments; ++i) {
        uint16_t base = i * 2;
        // First triangle
        indices.push_back(base);
        indices.push_back(base + 2);
        indices.push_back(base + 1);
        // Second triangle  
        indices.push_back(base + 1);
        indices.push_back(base + 2);
        indices.push_back(base + 3);
    }
    
    const bgfx::Memory* vmem = bgfx::copy(vertices.data(), static_cast<uint32_t>(vertices.size() * sizeof(GrassVertex)));
    blade_vbh_ = bgfx::createVertexBuffer(vmem, blade_layout_);
    
    const bgfx::Memory* imem = bgfx::copy(indices.data(), static_cast<uint32_t>(indices.size() * sizeof(uint16_t)));
    blade_ibh_ = bgfx::createIndexBuffer(imem);
}

void GrassRenderer::create_instance_buffer() {
    // Create instance layout
    instance_layout_
        .begin()
        .add(bgfx::Attrib::TexCoord7, 4, bgfx::AttribType::Float)  // x, z, rotation, scale
        .add(bgfx::Attrib::TexCoord6, 1, bgfx::AttribType::Float)  // seed
        .end();
    
    // Generate grass instances around the world center
    std::vector<GrassInstance> instances;
    
    float world_center_x = world_width_ / 2.0f;
    float world_center_z = world_height_ / 2.0f;
    
    // Cover area around playable zone
    float coverage_radius = 2000.0f;  // Cover 2km radius
    float margin = 50.0f;
    float town_radius = 200.0f;
    
    for (float x = world_center_x - coverage_radius; x <= world_center_x + coverage_radius; x += grass_spacing) {
        for (float z = world_center_z - coverage_radius; z <= world_center_z + coverage_radius; z += grass_spacing) {
            // Skip outside world bounds
            if (x < margin || x > world_width_ - margin || z < margin || z > world_height_ - margin) continue;
            
            // Skip town center
            float dx = x - world_center_x;
            float dz = z - world_center_z;
            if (std::abs(dx) < town_radius && std::abs(dz) < town_radius) continue;
            
            // Check distance from center
            float dist_sq = dx * dx + dz * dz;
            if (dist_sq > coverage_radius * coverage_radius) continue;
            
            // Use hash for consistent jitter
            float h = hash(x, z);
            float jitterX = (h - 0.5f) * grass_spacing * 0.8f;
            float jitterZ = (hash(x + 100.0f, z + 100.0f) - 0.5f) * grass_spacing * 0.8f;
            
            GrassInstance inst;
            inst.x = x + jitterX;
            inst.z = z + jitterZ;
            inst.rotation = hash(x + 50.0f, z) * 6.28318f;  // 0 to 2*PI
            inst.scale = 5.0f + hash(x, z + 50.0f) * 10.0f;  // 5-15 units tall
            inst.seed = hash(x + 200.0f, z + 200.0f);
            
            instances.push_back(inst);
        }
    }
    
    instance_count_ = static_cast<uint32_t>(instances.size());
    
    if (instance_count_ > 0) {
        const bgfx::Memory* mem = bgfx::copy(instances.data(), static_cast<uint32_t>(instances.size() * sizeof(GrassInstance)));
        instance_vbh_ = bgfx::createVertexBuffer(mem, instance_layout_, BGFX_BUFFER_ALLOW_RESIZE);
    }
}

void GrassRenderer::update(float delta_time, float current_time) {
    current_time_ = current_time;
}

void GrassRenderer::render(bgfx::ViewId view_id, const glm::mat4& view, const glm::mat4& projection,
                           const glm::vec3& camera_pos, const glm::mat4& light_space_matrix,
                           bgfx::TextureHandle shadow_map, bool shadows_enabled,
                           const glm::vec3& light_dir) {
    if (!initialized_ || instance_count_ == 0) return;
    if (!bgfx::isValid(program_) || !bgfx::isValid(blade_vbh_) || !bgfx::isValid(instance_vbh_)) return;
    
    // Use bgfx's predefined u_viewProj via setViewTransform
    bgfx::setViewTransform(view_id, glm::value_ptr(view), glm::value_ptr(projection));
    
    float camPos[4] = { camera_pos.x, camera_pos.y, camera_pos.z, 0.0f };
    bgfx::setUniform(u_cameraPos_, camPos);
    
    float lightDirVec[4] = { light_dir.x, light_dir.y, light_dir.z, 0.0f };
    bgfx::setUniform(u_lightDir_, lightDirVec);
    
    float grassParams[4] = { current_time_, wind_magnitude, wind_wave_length, wind_wave_period };
    bgfx::setUniform(u_grassParams_, grassParams);
    
    float fogParams[4] = { 0.12f, 0.14f, 0.2f, 300.0f };  // fogColor.rgb, fogStart
    bgfx::setUniform(u_fogParams_, fogParams);
    
    float fogParams2[4] = { grass_view_distance, shadows_enabled ? 1.0f : 0.0f, 0.0f, 0.0f };
    bgfx::setUniform(u_fogParams2_, fogParams2);
    
    float worldBounds[4] = { world_width_, world_height_, grass_spacing, grass_view_distance };
    bgfx::setUniform(u_worldBounds_, worldBounds);
    
    bgfx::setUniform(u_lightSpaceMatrix_, glm::value_ptr(light_space_matrix));
    
    if (bgfx::isValid(shadow_map)) {
        bgfx::setTexture(0, s_shadowMap_, shadow_map);
    }
    
    // Set vertex buffers - blade mesh and instance data
    bgfx::setVertexBuffer(0, blade_vbh_);
    bgfx::setVertexBuffer(1, instance_vbh_);
    bgfx::setIndexBuffer(blade_ibh_);
    bgfx::setInstanceDataBuffer(instance_vbh_, 0, instance_count_);
    
    // No culling for grass (visible from both sides)
    uint64_t state = BGFX_STATE_WRITE_RGB
                   | BGFX_STATE_WRITE_A
                   | BGFX_STATE_WRITE_Z
                   | BGFX_STATE_DEPTH_TEST_LESS
                   | BGFX_STATE_BLEND_ALPHA;
    bgfx::setState(state);
    
    bgfx::submit(view_id, program_);
}

void GrassRenderer::shutdown() {
    if (bgfx::isValid(blade_vbh_)) {
        bgfx::destroy(blade_vbh_);
        blade_vbh_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(blade_ibh_)) {
        bgfx::destroy(blade_ibh_);
        blade_ibh_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(instance_vbh_)) {
        bgfx::destroy(instance_vbh_);
        instance_vbh_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(program_)) {
        bgfx::destroy(program_);
        program_ = BGFX_INVALID_HANDLE;
    }
    
    // Destroy uniforms (Note: u_viewProj is bgfx predefined, not created by us)
    if (bgfx::isValid(u_cameraPos_)) bgfx::destroy(u_cameraPos_);
    if (bgfx::isValid(u_lightDir_)) bgfx::destroy(u_lightDir_);
    if (bgfx::isValid(u_grassParams_)) bgfx::destroy(u_grassParams_);
    if (bgfx::isValid(u_fogParams_)) bgfx::destroy(u_fogParams_);
    if (bgfx::isValid(u_fogParams2_)) bgfx::destroy(u_fogParams2_);
    if (bgfx::isValid(u_worldBounds_)) bgfx::destroy(u_worldBounds_);
    if (bgfx::isValid(u_lightSpaceMatrix_)) bgfx::destroy(u_lightSpaceMatrix_);
    if (bgfx::isValid(s_shadowMap_)) bgfx::destroy(s_shadowMap_);
    
    // Reset handles (u_viewProj is bgfx predefined)
    u_cameraPos_ = BGFX_INVALID_HANDLE;
    u_lightDir_ = BGFX_INVALID_HANDLE;
    u_grassParams_ = BGFX_INVALID_HANDLE;
    u_fogParams_ = BGFX_INVALID_HANDLE;
    u_fogParams2_ = BGFX_INVALID_HANDLE;
    u_worldBounds_ = BGFX_INVALID_HANDLE;
    u_lightSpaceMatrix_ = BGFX_INVALID_HANDLE;
    s_shadowMap_ = BGFX_INVALID_HANDLE;
    
    initialized_ = false;
}

} // namespace mmo
