#include "terrain_renderer.hpp"
#include "SDL3/SDL_gpu.h"
#include "engine/gpu/gpu_buffer.hpp"
#include "engine/gpu/gpu_uniforms.hpp"
#include "engine/gpu/gpu_device.hpp"
#include "engine/gpu/gpu_texture.hpp"
#include "engine/gpu/gpu_types.hpp"
#include "engine/gpu/pipeline_registry.hpp"
#include "engine/heightmap.hpp"
#include "glm/ext/matrix_float4x4.hpp"
#include "glm/ext/vector_float2.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>
#include <SDL3/SDL_log.h>
#include <SDL3_image/SDL_image.h>

namespace mmo::engine::render {

namespace gpu = mmo::engine::gpu;

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
    material_sampler_ = gpu::GPUSampler::create(device, gpu::SamplerConfig::anisotropic(8.0f));
    if (!material_sampler_) {
        SDL_Log("TerrainRenderer::init: Failed to create grass sampler");
        return false;
    }
    
    load_terrain_textures();
    // Note: terrain mesh will be generated when heightmap is received
    // For now, generate a flat placeholder
    generate_terrain_mesh();
    
    return true;
}

void TerrainRenderer::set_heightmap(const engine::Heightmap& heightmap) {
    // Store CPU-side copy for height queries
    heightmap_ = std::make_unique<engine::Heightmap>(heightmap);

    // Upload to GPU texture
    upload_heightmap_texture();

    // Regenerate terrain mesh using new heightmap
    generate_terrain_mesh();
}

void TerrainRenderer::update_splatmap(const uint8_t* data, uint32_t resolution) {
    if (!device_ || !data) return;

    // Recreate splatmap texture with new data
    splatmap_texture_ = gpu::GPUTexture::create_2d(
        *device_,
        resolution, resolution,
        gpu::TextureFormat::RGBA8,
        data,
        false  // No mipmaps for splatmap
    );

    if (!splatmap_texture_) {
        SDL_Log("TerrainRenderer::update_splatmap: Failed to update splatmap texture");
    }
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

    // Release terrain textures
    material_array_texture_.reset();
    splatmap_texture_.reset();

    material_sampler_.reset();
    heightmap_texture_.reset();
    heightmap_.reset();
    device_ = nullptr;
    pipeline_registry_ = nullptr;
    index_count_ = 0;
}

void TerrainRenderer::load_terrain_textures() {
    if (!device_) return;

    // Load individual material textures using SDL_image
    const char* texture_paths[4] = {
        "assets/textures/grass_seamless.png",
        "assets/textures/dirt_seamless.png",
        "assets/textures/rock_seamless.png",
        "assets/textures/sand_seamless.png"
    };

    SDL_Surface* loaded_surfaces[4] = {nullptr, nullptr, nullptr, nullptr};
    SDL_Surface* converted_surfaces[4] = {nullptr, nullptr, nullptr, nullptr};
    const void* layer_data[4] = {nullptr, nullptr, nullptr, nullptr};
    int tex_width = 0, tex_height = 0;

    // Load all textures and convert to RGBA8
    bool all_loaded = true;
    for (int i = 0; i < 4; ++i) {
        loaded_surfaces[i] = IMG_Load(texture_paths[i]);
        if (!loaded_surfaces[i]) {
            SDL_Log("TerrainRenderer::load_terrain_textures: Failed to load %s: %s",
                    texture_paths[i], SDL_GetError());
            all_loaded = false;
            break;
        }

        // Convert to RGBA8 format
        converted_surfaces[i] = SDL_ConvertSurface(loaded_surfaces[i], SDL_PIXELFORMAT_RGBA32);
        if (!converted_surfaces[i]) {
            SDL_Log("TerrainRenderer::load_terrain_textures: Failed to convert %s to RGBA8: %s",
                    texture_paths[i], SDL_GetError());
            all_loaded = false;
            break;
        }

        // Verify all textures have the same dimensions
        if (i == 0) {
            tex_width = converted_surfaces[i]->w;
            tex_height = converted_surfaces[i]->h;
        } else if (converted_surfaces[i]->w != tex_width || converted_surfaces[i]->h != tex_height) {
            SDL_Log("TerrainRenderer::load_terrain_textures: Texture size mismatch for %s",
                    texture_paths[i]);
            all_loaded = false;
            break;
        }

        layer_data[i] = converted_surfaces[i]->pixels;
    }

    // Create Texture2DArray with all 4 material layers
    if (all_loaded && tex_width > 0 && tex_height > 0) {
        material_array_texture_ = gpu::GPUTexture::create_2d_array(
            *device_,
            tex_width, tex_height,
            4,  // 4 layers: grass, dirt, rock, sand
            gpu::TextureFormat::RGBA8,
            layer_data
        );

        if (!material_array_texture_) {
            SDL_Log("TerrainRenderer::load_terrain_textures: Failed to create material array texture");
        }
    }

    // Clean up surfaces
    for (int i = 0; i < 4; ++i) {
        if (converted_surfaces[i]) {
            SDL_DestroySurface(converted_surfaces[i]);
        }
        if (loaded_surfaces[i]) {
            SDL_DestroySurface(loaded_surfaces[i]);
        }
    }

    // Load splatmap texture
    splatmap_texture_ = gpu::GPUTexture::load_from_file(
        *device_,
        "assets/textures/terrain_splatmap.png",
        false
    );

    if (!splatmap_texture_) {
        SDL_Log("TerrainRenderer::load_terrain_textures: Failed to load splatmap texture");
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
        float nx = 0.0f, ny = 1.0f, nz = 0.0f;
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

    float cell_size = 25.0f;  // ORIGINAL VALUE - reverting all changes to test shadows
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

            // Calculate normal from heightmap
            vertex.normal = get_normal(x, z);

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
                             const glm::vec3& camera_pos,
                             const glm::vec3& light_dir,
                             const SDL_GPUTextureSamplerBinding* shadow_bindings,
                             int shadow_binding_count) {
    // Skip rendering if required resources aren't available
    if (!pipeline_registry_ || !vertex_buffer_ || !index_buffer_ || !pass || !cmd) return;
    if (!material_array_texture_ || !splatmap_texture_ || !material_sampler_) {
        SDL_Log("TerrainRenderer::render: Terrain textures/sampler not ready, skipping");
        return;
    }
    
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
    
    SDL_PushGPUVertexUniformData(cmd, 0, &transform_uniforms, sizeof(transform_uniforms));
    
    // Push fragment uniforms (lighting data)
    TerrainLightingUniforms lighting_uniforms;
    lighting_uniforms.fogColor = fog_color_;
    lighting_uniforms.fogStart = fog_start_;
    lighting_uniforms.fogEnd = fog_end_;
    lighting_uniforms.world_size = world_width_;  // Assumes square terrain
    lighting_uniforms.lightDir = light_dir;
    lighting_uniforms._padding1 = 0.0f;
    
    SDL_PushGPUFragmentUniformData(cmd, 0, &lighting_uniforms, sizeof(lighting_uniforms));

    // Bind material texture array at slot 0 (4 layers: grass, dirt, rock, sand)
    SDL_GPUTextureSamplerBinding material_binding = {
        material_array_texture_->handle(), material_sampler_->handle()
    };
    SDL_BindGPUFragmentSamplers(pass, 0, &material_binding, 1);

    // Bind splatmap texture at slot 1
    SDL_GPUTextureSamplerBinding splatmap_binding = {
        splatmap_texture_->handle(), material_sampler_->handle()
    };
    SDL_BindGPUFragmentSamplers(pass, 1, &splatmap_binding, 1);

    // Bind shadow cascade textures at slots 2-5
    if (shadow_bindings && shadow_binding_count > 0) {
        SDL_BindGPUFragmentSamplers(pass, 2, shadow_bindings, shadow_binding_count);
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
    material_sampler_ = gpu::GPUSampler::create(*device_, gpu::SamplerConfig::anisotropic(level));
    if (!material_sampler_) {
        SDL_Log("TerrainRenderer::set_anisotropic_filter: Failed to recreate grass sampler with level %.1f", level);
    }
}

void TerrainRenderer::render_shadow(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd,
                                     const glm::mat4& light_view_projection) {
    if (!pipeline_registry_ || !vertex_buffer_ || !index_buffer_ || !pass || !cmd) return;

    auto* pipeline = pipeline_registry_->get_shadow_terrain_pipeline();
    if (!pipeline) return;

    pipeline->bind(pass);

    gpu::ShadowTerrainUniforms shadow_uniforms = {};
    shadow_uniforms.lightViewProjection = light_view_projection;
    SDL_PushGPUVertexUniformData(cmd, 0, &shadow_uniforms, sizeof(shadow_uniforms));

    SDL_GPUBufferBinding vb_binding = { vertex_buffer_->handle(), 0 };
    SDL_BindGPUVertexBuffers(pass, 0, &vb_binding, 1);

    SDL_GPUBufferBinding ib_binding = { index_buffer_->handle(), 0 };
    SDL_BindGPUIndexBuffer(pass, &ib_binding, SDL_GPU_INDEXELEMENTSIZE_32BIT);

    SDL_DrawGPUIndexedPrimitives(pass, index_count_, 1, 0, 0, 0);
}

} // namespace mmo::engine::render
