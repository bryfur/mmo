#include "grass_renderer.hpp"
#include "SDL3/SDL_gpu.h"
#include "engine/gpu/gpu_buffer.hpp"
#include "engine/gpu/gpu_device.hpp"
#include "engine/gpu/gpu_texture.hpp"
#include "engine/gpu/pipeline_registry.hpp"
#include "glm/common.hpp"
#include "glm/ext/matrix_float4x4.hpp"
#include "glm/ext/vector_float2.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "glm/geometric.hpp"
#include "glm/trigonometric.hpp"
#include <SDL3/SDL_log.h>
#include <cmath>
#include <cstdint>
#include <vector>

namespace mmo::engine::render {

namespace gpu = mmo::engine::gpu;

GrassRenderer::GrassRenderer() = default;

GrassRenderer::~GrassRenderer() {
    shutdown();
}

bool GrassRenderer::init(gpu::GPUDevice& device, gpu::PipelineRegistry& pipeline_registry,
                         float world_width, float world_height) {
    if (initialized_) return true;
    
    SDL_Log("Initializing grass renderer (SDL3 GPU API)...");
    
    device_ = &device;
    pipeline_registry_ = &pipeline_registry;
    world_width_ = world_width;
    world_height_ = world_height;
    
    // Create grass sampler with linear filtering
    grass_sampler_ = gpu::GPUSampler::create(device, gpu::SamplerConfig::linear_clamp());
    if (!grass_sampler_) {
        SDL_Log("GrassRenderer::init: Failed to create grass sampler");
        return false;
    }
    
    // Load grass texture
    grass_texture_ = gpu::GPUTexture::load_from_file(
        device,
        "assets/textures/grass_blade.png",
        false  // No mipmaps
    );
    
    if (!grass_texture_) {
        SDL_Log("GrassRenderer::init: Failed to load grass texture, using fallback color");
        // Continue without texture - shader will use vertex colors
    }
    
    initialized_ = true;
    SDL_Log("Grass renderer initialized");
    return true;
}

void GrassRenderer::update(float delta_time, float current_time) {
    current_time_ = current_time;
}

float GrassRenderer::hash(glm::vec2 p) {
    return glm::fract(std::sin(glm::dot(p, glm::vec2(127.1f, 311.7f))) * 43758.5453f);
}

glm::vec2 GrassRenderer::hash2(glm::vec2 p) {
    return glm::fract(glm::sin(glm::vec2(
        glm::dot(p, glm::vec2(127.1f, 311.7f)),
        glm::dot(p, glm::vec2(269.5f, 183.3f))
    )) * 43758.5453f);
}

float GrassRenderer::get_terrain_height(float x, float z) const {
    if (terrain_height_func_) {
        return terrain_height_func_(x, z);
    }
    return 0.0f;
}

void GrassRenderer::generate_grass_geometry(const glm::vec3& camera_pos) {
    if (!device_) return;
    
    // Calculate grid bounds around camera
    int grid_radius = static_cast<int>(grass_view_distance / grass_spacing);
    
    std::vector<GrassVertex> vertices;
    std::vector<uint32_t> indices;
    
    // Reserve approximate space
    int estimated_blades = (2 * grid_radius + 1) * (2 * grid_radius + 1);
    vertices.reserve(estimated_blades * 8);  // ~8 vertices per blade
    indices.reserve(estimated_blades * 12);  // ~12 indices per blade
    
    // Snap camera to grid
    glm::vec2 camera_grid = glm::floor(glm::vec2(camera_pos.x, camera_pos.z) / grass_spacing) * grass_spacing;
    
    uint32_t vertex_offset = 0;
    
    for (int gz = -grid_radius; gz <= grid_radius; ++gz) {
        for (int gx = -grid_radius; gx <= grid_radius; ++gx) {
            glm::vec2 world_pos_2d = camera_grid + glm::vec2(gx, gz) * grass_spacing;
            
            // World bounds check
            float margin = 50.0f;
            if (world_pos_2d.x < margin || world_pos_2d.x > world_width_ - margin ||
                world_pos_2d.y < margin || world_pos_2d.y > world_height_ - margin) {
                continue;
            }
            
            // Skip town center area
            glm::vec2 town_center(world_width_ * 0.5f, world_height_ * 0.5f);
            if (std::abs(world_pos_2d.x - town_center.x) < 200.0f && 
                std::abs(world_pos_2d.y - town_center.y) < 200.0f) {
                continue;
            }
            
            // Distance culling
            float dist = glm::length(world_pos_2d - glm::vec2(camera_pos.x, camera_pos.z));
            if (dist > grass_view_distance) continue;
            
            // Use world position as seed for consistent grass properties
            glm::vec2 seed = glm::floor(world_pos_2d / grass_spacing);
            
            // Jitter position within cell for natural look
            glm::vec2 jitter = (hash2(seed) - 0.5f) * grass_spacing * 0.8f;
            float grass_x = world_pos_2d.x + jitter.x;
            float grass_z = world_pos_2d.y + jitter.y;
            float terrain_y = get_terrain_height(grass_x, grass_z);
            glm::vec3 base_pos(grass_x, terrain_y, grass_z);
            
            // Random grass properties from hash
            float h1 = hash(seed);
            float h2 = hash(seed + glm::vec2(1.0f, 0.0f));
            float h3 = hash(seed + glm::vec2(0.0f, 1.0f));
            float h4 = hash(seed + glm::vec2(1.0f, 1.0f));
            float h5 = hash(seed + glm::vec2(3.0f, 3.0f));
            
            float orientation = h1 * 3.14159f;
            float height_mult = h5 * h5;  // Squared for more short grass
            float blade_height = 3.0f + h2 * 6.0f + height_mult * 8.0f;  // 3-17 units
            float blade_width = 0.8f + h3 * 0.6f;  // 0.8-1.4 units
            
            // Tilt for natural look
            float tilt_x = (hash(seed + glm::vec2(2.0f, 0.0f)) - 0.5f) * 0.4f;
            float tilt_z = (hash(seed + glm::vec2(0.0f, 2.0f)) - 0.5f) * 0.4f;
            glm::vec3 up = glm::normalize(glm::vec3(tilt_x, 1.0f, tilt_z));
            
            // Width direction
            glm::vec3 width_dir(std::cos(orientation), 0.0f, std::sin(orientation));
            width_dir = glm::normalize(glm::cross(up, width_dir));
            
            // Color gradient: dark at base, bright at tip
            glm::vec4 base_color(0.08f, 0.18f, 0.04f, 1.0f);
            glm::vec4 tip_color(0.25f, 0.55f, 0.12f, 1.0f);
            
            // LOD: fewer segments at distance
            int segments = dist < 150.0f ? 4 : (dist < 350.0f ? 3 : 2);
            float segment_step = 1.0f / static_cast<float>(segments);
            
            // Bezier control points for blade curve
            glm::vec3 p0 = base_pos;
            glm::vec3 p1 = base_pos + up * blade_height * 0.5f;
            glm::vec3 p2 = base_pos + up * blade_height;
            
            // Face normal for lighting
            glm::vec3 to_camera = glm::normalize(camera_pos - base_pos);
            glm::vec3 blade_normal = glm::normalize(glm::cross(up, width_dir));
            blade_normal = glm::normalize(blade_normal + to_camera * 0.3f);
            
            // Generate vertices along blade
            float base_width = blade_width * 0.5f;
            uint32_t blade_start_vertex = vertex_offset;
            
            for (int i = 0; i <= segments; ++i) {
                float t = static_cast<float>(i) * segment_step;
                
                // Quadratic Bezier
                float t1 = 1.0f - t;
                glm::vec3 pos = t1 * t1 * p0 + 2.0f * t1 * t * p1 + t * t * p2;
                
                // Taper width toward tip
                float w = base_width * (1.0f - t * 0.9f);
                
                // Interpolate color
                glm::vec4 color = glm::mix(base_color, tip_color, t);
                
                // Left vertex
                GrassVertex left_vertex;
                left_vertex.position = pos - width_dir * w;
                left_vertex.normal = blade_normal;
                left_vertex.texCoord = glm::vec2(0.0f, t);
                left_vertex.color = color;
                vertices.push_back(left_vertex);
                
                // Right vertex
                GrassVertex right_vertex;
                right_vertex.position = pos + width_dir * w;
                right_vertex.normal = blade_normal;
                right_vertex.texCoord = glm::vec2(1.0f, t);
                right_vertex.color = color;
                vertices.push_back(right_vertex);
            }
            
            // Generate indices for triangle strip as triangles
            for (int i = 0; i < segments; ++i) {
                uint32_t base_idx = blade_start_vertex + i * 2;
                // First triangle
                indices.push_back(base_idx);
                indices.push_back(base_idx + 2);
                indices.push_back(base_idx + 1);
                // Second triangle
                indices.push_back(base_idx + 1);
                indices.push_back(base_idx + 2);
                indices.push_back(base_idx + 3);
            }
            
            vertex_offset = static_cast<uint32_t>(vertices.size());
        }
    }
    
    if (vertices.empty()) {
        index_count_ = 0;
        return;
    }
    
    // Create vertex buffer
    vertex_buffer_ = gpu::GPUBuffer::create_static(
        *device_,
        gpu::GPUBuffer::Type::Vertex,
        vertices.data(),
        vertices.size() * sizeof(GrassVertex)
    );
    
    if (!vertex_buffer_) {
        SDL_Log("GrassRenderer::generate_grass_geometry: Failed to create vertex buffer");
        return;
    }
    
    // Create index buffer
    index_buffer_ = gpu::GPUBuffer::create_static(
        *device_,
        gpu::GPUBuffer::Type::Index,
        indices.data(),
        indices.size() * sizeof(uint32_t)
    );
    
    if (!index_buffer_) {
        SDL_Log("GrassRenderer::generate_grass_geometry: Failed to create index buffer");
        vertex_buffer_.reset();
        return;
    }
    
    index_count_ = static_cast<uint32_t>(indices.size());
}

void GrassRenderer::render(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd,
                           const glm::mat4& view, const glm::mat4& projection,
                           const glm::vec3& camera_pos, const glm::vec3& light_dir) {
    if (!initialized_ || !pipeline_registry_ || !pass || !cmd) return;

    // Skip rendering if grass texture/sampler aren't available
    if (!grass_texture_ || !grass_sampler_) return;

    // Regenerate grass geometry if camera has moved significantly
    // or if this is the first frame
    if (!vertex_buffer_ || !index_buffer_ || index_count_ == 0) {
        generate_grass_geometry(camera_pos);
    }

    if (!vertex_buffer_ || !index_buffer_ || index_count_ == 0) return;
    
    // Get grass pipeline
    auto* pipeline = pipeline_registry_->get_grass_pipeline();
    if (!pipeline) {
        SDL_Log("GrassRenderer::render: Failed to get grass pipeline");
        return;
    }
    
    // Bind pipeline
    pipeline->bind(pass);
    
    // Push vertex uniforms (transform and wind data)
    GrassTransformUniforms transform_uniforms;
    transform_uniforms.view_projection = projection * view;
    transform_uniforms.camera_pos = camera_pos;
    transform_uniforms.time = current_time_;
    transform_uniforms.wind_strength = wind_magnitude;
    transform_uniforms.wind_direction = glm::vec3(1.0f, 0.0f, 0.3f);  // Default wind direction
    
    SDL_PushGPUVertexUniformData(cmd, 0, &transform_uniforms, sizeof(transform_uniforms));
    
    // Push fragment uniforms (fog data)
    GrassLightingUniforms lighting_uniforms;
    lighting_uniforms.fog_color = glm::vec3(0.12f, 0.14f, 0.2f);
    lighting_uniforms.fog_start = 300.0f;
    lighting_uniforms.fog_end = grass_view_distance;
    lighting_uniforms.fog_enabled = 1;
    lighting_uniforms._padding[0] = 0;
    lighting_uniforms._padding[1] = 0;
    
    SDL_PushGPUFragmentUniformData(cmd, 0, &lighting_uniforms, sizeof(lighting_uniforms));
    
    // Bind grass texture and sampler (already validated at function start)
    SDL_GPUTextureSamplerBinding grass_binding = {
        grass_texture_->handle(),
        grass_sampler_->handle()
    };
    SDL_BindGPUFragmentSamplers(pass, 0, &grass_binding, 1);
    
    // Bind vertex buffer
    SDL_GPUBufferBinding vb_binding = {
        vertex_buffer_->handle(),
        0
    };
    SDL_BindGPUVertexBuffers(pass, 0, &vb_binding, 1);
    
    // Bind index buffer
    SDL_GPUBufferBinding ib_binding = {
        index_buffer_->handle(),
        0
    };
    SDL_BindGPUIndexBuffer(pass, &ib_binding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
    
    // Draw indexed primitives
    SDL_DrawGPUIndexedPrimitives(pass, index_count_, 1, 0, 0, 0);
}

void GrassRenderer::shutdown() {
    vertex_buffer_.reset();
    index_buffer_.reset();
    grass_texture_.reset();
    grass_sampler_.reset();
    heightmap_texture_ = nullptr;
    terrain_height_func_ = nullptr;
    device_ = nullptr;
    pipeline_registry_ = nullptr;
    index_count_ = 0;
    initialized_ = false;
}

} // namespace mmo::engine::render
