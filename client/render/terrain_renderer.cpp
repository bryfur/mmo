#include "terrain_renderer.hpp"
#include <cmath>
#include <vector>
#include <SDL3_image/SDL_image.h>
#include <SDL3/SDL_log.h>

namespace mmo {

TerrainRenderer::TerrainRenderer() = default;

TerrainRenderer::~TerrainRenderer() {
    shutdown();
}

bool TerrainRenderer::init(gpu::GPUDevice& device, gpu::PipelineRegistry& pipeline_registry,
                           float world_width, float world_height) {
    device_ = &device;
    pipeline_registry_ = &pipeline_registry;
    world_width_ = world_width;
    world_height_ = world_height;
    
    // Create grass sampler with linear filtering and repeat addressing
    grass_sampler_ = gpu::GPUSampler::create(device, gpu::SamplerConfig::anisotropic(8.0f));
    if (!grass_sampler_) {
        SDL_Log("TerrainRenderer::init: Failed to create grass sampler");
        return false;
    }
    
    load_grass_texture();
    // Note: terrain mesh will be generated when heightmap is received
    // For now, generate a flat placeholder
    generate_terrain_mesh();
    
    return true;
}

void TerrainRenderer::set_heightmap(const HeightmapChunk& heightmap) {
    // Store CPU-side copy for height queries
    heightmap_ = std::make_unique<HeightmapChunk>(heightmap);
    
    // Upload to GPU texture
    upload_heightmap_texture();
    
    // Regenerate terrain mesh using new heightmap
    generate_terrain_mesh();
}

void TerrainRenderer::upload_heightmap_texture() {
    if (!heightmap_ || !device_) return;
    
    // Release old texture if exists
    heightmap_texture_.reset();
    
    // Create R16 texture for heightmap (16-bit unsigned normalized)
    // This preserves full precision from server-provided height data
    heightmap_texture_ = gpu::GPUTexture::create_2d(
        *device_,
        heightmap_->resolution, heightmap_->resolution,
        gpu::TextureFormat::R16,
        heightmap_->height_data.data(),
        false  // No mipmaps for heightmap
    );
    
    if (!heightmap_texture_) {
        SDL_Log("TerrainRenderer::upload_heightmap_texture: Failed to create heightmap texture");
    }
}

void TerrainRenderer::shutdown() {
    vertex_buffer_.reset();
    index_buffer_.reset();
    grass_texture_.reset();
    grass_sampler_.reset();
    heightmap_texture_.reset();
    heightmap_.reset();
    device_ = nullptr;
    pipeline_registry_ = nullptr;
    index_count_ = 0;
}

void TerrainRenderer::load_grass_texture() {
    if (!device_) return;
    
    grass_texture_ = gpu::GPUTexture::load_from_file(
        *device_,
        "assets/textures/grass_seamless.png",
        false  // No mipmaps until GPUTexture implements mipmap generation
    );
    
    if (!grass_texture_) {
        SDL_Log("TerrainRenderer::load_grass_texture: Failed to load grass texture");
    }
}

float TerrainRenderer::get_height(float x, float z) const {
    // Sample from CPU-side heightmap if available
    if (heightmap_) {
        return heightmap_->get_height_world(x, z);
    }
    // Fallback: return 0 (flat) if no heightmap yet
    return 0.0f;
}

glm::vec3 TerrainRenderer::get_normal(float x, float z) const {
    if (heightmap_) {
        float nx, ny, nz;
        heightmap_->get_normal_world(x, z, nx, ny, nz);
        return glm::vec3(nx, ny, nz);
    }
    // Fallback: return up vector
    return glm::vec3(0.0f, 1.0f, 0.0f);
}

void TerrainRenderer::generate_terrain_mesh() {
    if (!device_) return;
    
    float margin = 5000.0f;
    float start_x = -margin;
    float start_z = -margin;
    float end_x = world_width_ + margin;
    float end_z = world_height_ + margin;
    
    float cell_size = 25.0f;
    int cells_x = static_cast<int>((end_x - start_x) / cell_size);
    int cells_z = static_cast<int>((end_z - start_z) / cell_size);
    
    float tex_scale = 0.01f;
    
    std::vector<TerrainVertex> vertices;
    std::vector<uint32_t> indices;
    
    // Generate vertices
    for (int iz = 0; iz <= cells_z; ++iz) {
        for (int ix = 0; ix <= cells_x; ++ix) {
            float x = start_x + ix * cell_size;
            float z = start_z + iz * cell_size;
            float y = get_height(x, z);
            
            TerrainVertex vertex;
            vertex.position = glm::vec3(x, y, z);
            vertex.texCoord = glm::vec2(x * tex_scale, z * tex_scale);
            
            // Color tint based on position
            float world_center_x = world_width_ / 2.0f;
            float world_center_z = world_height_ / 2.0f;
            float dx = x - world_center_x;
            float dz = z - world_center_z;
            float dist = std::sqrt(dx * dx + dz * dz);
            
            float dist_factor = std::min(dist / 3000.0f, 1.0f);
            float height_factor = std::min(std::max(y / 100.0f, 0.0f), 1.0f);
            
            float r = 0.95f + dist_factor * 0.05f;
            float g = 1.0f - dist_factor * 0.05f - height_factor * 0.05f;
            float b = 0.9f + dist_factor * 0.05f;
            
            vertex.color = glm::vec4(r, g, b, 1.0f);
            
            vertices.push_back(vertex);
        }
    }
    
    // Generate indices
    for (int iz = 0; iz < cells_z; ++iz) {
        for (int ix = 0; ix < cells_x; ++ix) {
            uint32_t tl = iz * (cells_x + 1) + ix;
            uint32_t tr = tl + 1;
            uint32_t bl = (iz + 1) * (cells_x + 1) + ix;
            uint32_t br = bl + 1;
            
            indices.push_back(tl);
            indices.push_back(bl);
            indices.push_back(tr);
            indices.push_back(tr);
            indices.push_back(bl);
            indices.push_back(br);
        }
    }
    
    index_count_ = static_cast<uint32_t>(indices.size());
    
    // Create vertex buffer
    vertex_buffer_ = gpu::GPUBuffer::create_static(
        *device_,
        gpu::GPUBuffer::Type::Vertex,
        vertices.data(),
        vertices.size() * sizeof(TerrainVertex)
    );
    
    if (!vertex_buffer_) {
        SDL_Log("TerrainRenderer::generate_terrain_mesh: Failed to create vertex buffer");
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
        SDL_Log("TerrainRenderer::generate_terrain_mesh: Failed to create index buffer");
        vertex_buffer_.reset();
        return;
    }
}

void TerrainRenderer::render(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd,
                             const glm::mat4& view, const glm::mat4& projection,
                             const glm::vec3& camera_pos, const glm::mat4& light_space_matrix,
                             SDL_GPUTexture* shadow_map, SDL_GPUSampler* shadow_sampler,
                             bool shadows_enabled,
                             SDL_GPUTexture* ssao_texture, SDL_GPUSampler* ssao_sampler,
                             bool ssao_enabled,
                             const glm::vec3& light_dir, const glm::vec2& screen_size) {
    if (!pipeline_registry_ || !vertex_buffer_ || !index_buffer_ || !pass || !cmd) return;
    
    // Get terrain pipeline
    auto* pipeline = pipeline_registry_->get_terrain_pipeline();
    if (!pipeline) {
        SDL_Log("TerrainRenderer::render: Failed to get terrain pipeline");
        return;
    }
    
    // Bind pipeline
    pipeline->bind(pass);
    
    // Push vertex uniforms (transform data)
    TerrainTransformUniforms transform_uniforms;
    transform_uniforms.view = view;
    transform_uniforms.projection = projection;
    transform_uniforms.cameraPos = camera_pos;
    transform_uniforms._padding0 = 0.0f;
    transform_uniforms.lightSpaceMatrix = light_space_matrix;
    
    SDL_PushGPUVertexUniformData(cmd, 0, &transform_uniforms, sizeof(transform_uniforms));
    
    // Push fragment uniforms (lighting data)
    TerrainLightingUniforms lighting_uniforms;
    lighting_uniforms.fogColor = fog_color_;
    lighting_uniforms.fogStart = fog_start_;
    lighting_uniforms.fogEnd = fog_end_;
    lighting_uniforms.shadowsEnabled = shadows_enabled ? 1 : 0;
    lighting_uniforms.ssaoEnabled = ssao_enabled ? 1 : 0;
    lighting_uniforms._padding0 = 0.0f;
    lighting_uniforms.lightDir = light_dir;
    lighting_uniforms._padding1 = 0.0f;
    lighting_uniforms.screenSize = screen_size;
    lighting_uniforms._padding2 = glm::vec2(0.0f);
    
    SDL_PushGPUFragmentUniformData(cmd, 0, &lighting_uniforms, sizeof(lighting_uniforms));
    
    // Bind textures and samplers
    // Slot 0: grass texture
    if (grass_texture_ && grass_sampler_) {
        SDL_GPUTextureSamplerBinding grass_binding = {
            grass_texture_->handle(),
            grass_sampler_->handle()
        };
        SDL_BindGPUFragmentSamplers(pass, 0, &grass_binding, 1);
    }
    
    // Slot 1: shadow map
    if (shadow_map && shadow_sampler) {
        SDL_GPUTextureSamplerBinding shadow_binding = {
            shadow_map,
            shadow_sampler
        };
        SDL_BindGPUFragmentSamplers(pass, 1, &shadow_binding, 1);
    }
    
    // Slot 2: SSAO texture
    if (ssao_texture && ssao_sampler) {
        SDL_GPUTextureSamplerBinding ssao_binding = {
            ssao_texture,
            ssao_sampler
        };
        SDL_BindGPUFragmentSamplers(pass, 2, &ssao_binding, 1);
    }
    
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

void TerrainRenderer::set_anisotropic_filter(float level) {
    if (!device_) {
        return;
    }
    
    // Clamp level to reasonable range (1.0 = no anisotropy, 16.0 = max typical)
    level = std::max(1.0f, std::min(16.0f, level));
    
    // Recreate grass sampler with new anisotropy level
    grass_sampler_ = gpu::GPUSampler::create(*device_, gpu::SamplerConfig::anisotropic(level));
    if (!grass_sampler_) {
        SDL_Log("TerrainRenderer::set_anisotropic_filter: Failed to recreate grass sampler with level %.1f", level);
    }
}

} // namespace mmo
