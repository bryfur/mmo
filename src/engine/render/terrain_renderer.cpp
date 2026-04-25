#include "terrain_renderer.hpp"
#include "SDL3/SDL_gpu.h"
#include "engine/gpu/gpu_buffer.hpp"
#include "engine/gpu/gpu_uniforms.hpp"
#include "engine/gpu/gpu_device.hpp"
#include "engine/gpu/gpu_texture.hpp"
#include "engine/gpu/gpu_types.hpp"
#include "engine/gpu/pipeline_registry.hpp"
#include "engine/heightmap.hpp"
#include "engine/memory/arena.hpp"
#include "engine/scene/frustum.hpp"
#include <limits>
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
    generate_shadow_terrain_mesh();

    return true;
}

void TerrainRenderer::set_heightmap(const engine::Heightmap& heightmap) {
    // Store CPU-side copy for height queries
    heightmap_ = std::make_unique<engine::Heightmap>(heightmap);

    // Upload to GPU texture
    upload_heightmap_texture();

    // Regenerate terrain mesh using new heightmap (both high and low LOD)
    generate_terrain_mesh();
    generate_shadow_terrain_mesh();
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
    shadow_vertex_buffer_.reset();
    shadow_index_buffer_.reset();

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

    // Reduced margin: 5000 added 10000 units of terrain ring outside the world
    // (400x wider x 400x taller at cell=25 = huge triangle count). 500 is enough
    // for visual continuity beyond world edge; tile culling hides the rest anyway.
    float margin = 500.0f;
    float start_x = -margin;
    float start_z = -margin;
    float end_x = world_width_ + margin;
    float end_z = world_height_ + margin;

    float cell_size = 25.0f;
    int cells_x = static_cast<int>((end_x - start_x) / cell_size);
    int cells_z = static_cast<int>((end_z - start_z) / cell_size);

    // Chunk into tiles of TILE_CELLS × TILE_CELLS cells. At cell=25, tile=32 cells =
    // 800 world units. For a 42000-unit world this yields ~52x52 = ~2700 tiles;
    // typically only 5-15% are in the view frustum = 10–20x less geometry drawn.
    constexpr int TILE_CELLS = 32;
    int tiles_x = (cells_x + TILE_CELLS - 1) / TILE_CELLS;
    int tiles_z = (cells_z + TILE_CELLS - 1) / TILE_CELLS;

    float tex_scale = 0.01f;

    // One-shot setup: vertex+index buffers are built once, uploaded, and discarded.
    // A single Arena chunk replaces two heap-grown std::vectors.
    const size_t vertex_count_max = static_cast<size_t>(cells_x + 1) * static_cast<size_t>(cells_z + 1);
    const size_t index_count_max  = static_cast<size_t>(cells_x) * static_cast<size_t>(cells_z) * 6;
    const size_t arena_bytes =
        vertex_count_max * sizeof(TerrainVertex) +
        index_count_max  * sizeof(uint32_t) +
        4096;
    memory::Arena scratch(arena_bytes);

    auto* vertices = static_cast<TerrainVertex*>(
        scratch.allocate(vertex_count_max * sizeof(TerrainVertex), alignof(TerrainVertex)));
    auto* indices  = static_cast<uint32_t*>(
        scratch.allocate(index_count_max * sizeof(uint32_t), alignof(uint32_t)));
    size_t vertex_count = 0;
    size_t index_count  = 0;

    for (int iz = 0; iz <= cells_z; ++iz) {
        for (int ix = 0; ix <= cells_x; ++ix) {
            float x = start_x + ix * cell_size;
            float z = start_z + iz * cell_size;
            float y = get_height(x, z);

            TerrainVertex vertex;
            vertex.position = glm::vec3(x, y, z);
            vertex.texCoord = glm::vec2(x * tex_scale, z * tex_scale);
            vertex.normal = get_normal(x, z);

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

            vertices[vertex_count++] = vertex;
        }
    }

    // Build indices per tile, track index ranges + AABBs for frustum culling.
    tiles_.clear();
    tiles_.reserve(tiles_x * tiles_z);
    for (int tz = 0; tz < tiles_z; ++tz) {
        for (int tx = 0; tx < tiles_x; ++tx) {
            int cell_x_start = tx * TILE_CELLS;
            int cell_z_start = tz * TILE_CELLS;
            int cell_x_end = std::min(cell_x_start + TILE_CELLS, cells_x);
            int cell_z_end = std::min(cell_z_start + TILE_CELLS, cells_z);

            TerrainTile tile;
            tile.first_index = static_cast<uint32_t>(index_count);

            float min_y =  std::numeric_limits<float>::infinity();
            float max_y = -std::numeric_limits<float>::infinity();

            for (int iz = cell_z_start; iz < cell_z_end; ++iz) {
                for (int ix = cell_x_start; ix < cell_x_end; ++ix) {
                    uint32_t tl = iz * (cells_x + 1) + ix;
                    uint32_t tr = tl + 1;
                    uint32_t bl = (iz + 1) * (cells_x + 1) + ix;
                    uint32_t br = bl + 1;
                    indices[index_count++] = tl;
                    indices[index_count++] = bl;
                    indices[index_count++] = tr;
                    indices[index_count++] = tr;
                    indices[index_count++] = bl;
                    indices[index_count++] = br;

                    min_y = std::min({min_y, vertices[tl].position.y, vertices[tr].position.y,
                                      vertices[bl].position.y, vertices[br].position.y});
                    max_y = std::max({max_y, vertices[tl].position.y, vertices[tr].position.y,
                                      vertices[bl].position.y, vertices[br].position.y});
                }
            }

            tile.index_count = static_cast<uint32_t>(index_count) - tile.first_index;
            tile.aabb_min = glm::vec3(start_x + cell_x_start * cell_size,
                                      min_y,
                                      start_z + cell_z_start * cell_size);
            tile.aabb_max = glm::vec3(start_x + cell_x_end * cell_size,
                                      max_y,
                                      start_z + cell_z_end * cell_size);
            if (tile.index_count > 0) tiles_.push_back(tile);
        }
    }

    index_count_ = static_cast<uint32_t>(index_count);

    vertex_buffer_ = gpu::GPUBuffer::create_static(
        *device_,
        gpu::GPUBuffer::Type::Vertex,
        vertices,
        vertex_count * sizeof(TerrainVertex)
    );

    if (!vertex_buffer_) {
        SDL_Log("TerrainRenderer::generate_terrain_mesh: Failed to create vertex buffer");
        return;
    }

    index_buffer_ = gpu::GPUBuffer::create_static(
        *device_,
        gpu::GPUBuffer::Type::Index,
        indices,
        index_count * sizeof(uint32_t)
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
                             int shadow_binding_count,
                             const scene::Frustum* frustum) {
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
    TerrainLightingUniforms lighting_uniforms{};
    lighting_uniforms.fogColor = fog_color_;
    lighting_uniforms.fogStart = fog_start_;
    lighting_uniforms.fogEnd = fog_end_;
    lighting_uniforms.world_size = world_width_;  // Assumes square terrain
    lighting_uniforms.lightDir = light_dir;
    lighting_uniforms._padding1 = 0.0f;
    lighting_uniforms.ambientStrength = ambient_strength_;
    lighting_uniforms.sunIntensity = sun_intensity_;

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

    if (cluster_light_data_ && cluster_offsets_ && cluster_indices_ && cluster_params_) {
        SDL_GPUBuffer* bufs[3] = { cluster_light_data_, cluster_offsets_, cluster_indices_ };
        SDL_BindGPUFragmentStorageBuffers(pass, 0, bufs, 3);
        SDL_PushGPUFragmentUniformData(cmd, 2, cluster_params_, static_cast<Uint32>(cluster_params_size_));
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

    // Tile-culled draw: iterate tiles, skip those outside the view frustum.
    // Fallback: if no frustum provided or no tiles, draw the whole mesh.
    if (frustum && !tiles_.empty()) {
        for (const auto& tile : tiles_) {
            if (!frustum->intersects_aabb(tile.aabb_min, tile.aabb_max)) continue;
            SDL_DrawGPUIndexedPrimitives(pass, tile.index_count, 1, tile.first_index, 0, 0);
        }
    } else {
        SDL_DrawGPUIndexedPrimitives(pass, index_count_, 1, 0, 0, 0);
    }
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

void TerrainRenderer::set_cluster_lighting(SDL_GPUBuffer* light_data,
                                           SDL_GPUBuffer* cluster_offsets,
                                           SDL_GPUBuffer* light_indices,
                                           const void* params, size_t params_size) {
    cluster_light_data_ = light_data;
    cluster_offsets_ = cluster_offsets;
    cluster_indices_ = light_indices;
    cluster_params_ = params;
    cluster_params_size_ = params_size;
}

void TerrainRenderer::render_shadow(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd,
                                     const glm::mat4& light_view_projection,
                                     const scene::Frustum* frustum) {
    if (!pipeline_registry_ || !pass || !cmd) return;

    // Prefer the low-LOD shadow mesh (4x coarser -> 16x fewer triangles per cascade).
    const bool using_shadow_mesh = (shadow_vertex_buffer_ && shadow_index_buffer_ && shadow_index_count_ > 0);
    gpu::GPUBuffer* vb = using_shadow_mesh ? shadow_vertex_buffer_.get() : vertex_buffer_.get();
    gpu::GPUBuffer* ib = using_shadow_mesh ? shadow_index_buffer_.get() : index_buffer_.get();
    uint32_t idx_count = using_shadow_mesh ? shadow_index_count_ : index_count_;
    const std::vector<TerrainTile>* tile_list = using_shadow_mesh ? &shadow_tiles_ : &tiles_;
    if (!vb || !ib || idx_count == 0) return;

    auto* pipeline = pipeline_registry_->get_shadow_terrain_pipeline();
    if (!pipeline) return;

    pipeline->bind(pass);

    gpu::ShadowTerrainUniforms shadow_uniforms = {};
    shadow_uniforms.lightViewProjection = light_view_projection;
    SDL_PushGPUVertexUniformData(cmd, 0, &shadow_uniforms, sizeof(shadow_uniforms));

    SDL_GPUBufferBinding vb_binding = { vb->handle(), 0 };
    SDL_BindGPUVertexBuffers(pass, 0, &vb_binding, 1);

    SDL_GPUBufferBinding ib_binding = { ib->handle(), 0 };
    SDL_BindGPUIndexBuffer(pass, &ib_binding, SDL_GPU_INDEXELEMENTSIZE_32BIT);

    // Per-cascade tile culling: cascade 0 (tight near) draws maybe 2-5 tiles,
    // cascade 3 (far) draws many but still skips half the world.
    if (frustum && !tile_list->empty()) {
        for (const auto& tile : *tile_list) {
            if (!frustum->intersects_aabb(tile.aabb_min, tile.aabb_max)) continue;
            SDL_DrawGPUIndexedPrimitives(pass, tile.index_count, 1, tile.first_index, 0, 0);
        }
    } else {
        SDL_DrawGPUIndexedPrimitives(pass, idx_count, 1, 0, 0, 0);
    }
}

void TerrainRenderer::generate_shadow_terrain_mesh() {
    if (!device_) return;

    // Coarser mesh: 4x the cell size of the main terrain (25 -> 100).
    // Shadow maps are 2048 at most; 100-unit cell spacing is finer than the
    // per-texel shadow resolution at typical camera ranges, so visual impact
    // is imperceptible while triangle count drops 16x.
    constexpr float kShadowCellSize = 100.0f;
    constexpr float kShadowMargin = 500.0f;  // tighter — shadow pass frustum-culls tiles anyway
    constexpr int TILE_CELLS = 16;  // 16 cells @ 100 units = 1600 world units per tile

    float start_x = -kShadowMargin;
    float start_z = -kShadowMargin;
    float end_x = world_width_ + kShadowMargin;
    float end_z = world_height_ + kShadowMargin;

    int cells_x = static_cast<int>((end_x - start_x) / kShadowCellSize);
    int cells_z = static_cast<int>((end_z - start_z) / kShadowCellSize);
    if (cells_x <= 0 || cells_z <= 0) return;

    int tiles_x = (cells_x + TILE_CELLS - 1) / TILE_CELLS;
    int tiles_z = (cells_z + TILE_CELLS - 1) / TILE_CELLS;

    std::vector<TerrainVertex> vertices;
    std::vector<uint32_t> indices;
    vertices.reserve(static_cast<size_t>(cells_x + 1) * (cells_z + 1));

    // Conservative MIN sample: each shadow vertex takes the minimum of the
    // high-detail heightmap over its cell neighborhood (±half-cell). Keeps
    // the simplified shadow surface at-or-below real terrain everywhere,
    // preventing phantom shadow blobs in sub-cell depressions. Small downward
    // sink covers floating-point + interpolation slack across the planar quad.
    constexpr float PROBE_STEP = 25.0f;
    constexpr int   PROBE_RADIUS = 2;
    constexpr float SHADOW_SINK = 5.0f;

    for (int iz = 0; iz <= cells_z; ++iz) {
        for (int ix = 0; ix <= cells_x; ++ix) {
            float x = start_x + ix * kShadowCellSize;
            float z = start_z + iz * kShadowCellSize;
            float y = get_height(x, z);
            for (int dz = -PROBE_RADIUS; dz <= PROBE_RADIUS; ++dz) {
                for (int dx = -PROBE_RADIUS; dx <= PROBE_RADIUS; ++dx) {
                    if (dx == 0 && dz == 0) continue;
                    y = std::min(y, get_height(x + dx * PROBE_STEP, z + dz * PROBE_STEP));
                }
            }
            TerrainVertex v{};
            v.position = glm::vec3(x, y - SHADOW_SINK, z);
            vertices.push_back(v);
        }
    }

    shadow_tiles_.clear();
    shadow_tiles_.reserve(static_cast<size_t>(tiles_x) * tiles_z);
    for (int tz = 0; tz < tiles_z; ++tz) {
        for (int tx = 0; tx < tiles_x; ++tx) {
            int cx0 = tx * TILE_CELLS;
            int cz0 = tz * TILE_CELLS;
            int cx1 = std::min(cx0 + TILE_CELLS, cells_x);
            int cz1 = std::min(cz0 + TILE_CELLS, cells_z);

            TerrainTile tile;
            tile.first_index = static_cast<uint32_t>(indices.size());
            float min_y =  std::numeric_limits<float>::infinity();
            float max_y = -std::numeric_limits<float>::infinity();

            for (int iz = cz0; iz < cz1; ++iz) {
                for (int ix = cx0; ix < cx1; ++ix) {
                    uint32_t tl = iz * (cells_x + 1) + ix;
                    uint32_t tr = tl + 1;
                    uint32_t bl = tl + (cells_x + 1);
                    uint32_t br = bl + 1;
                    indices.push_back(tl); indices.push_back(bl); indices.push_back(tr);
                    indices.push_back(tr); indices.push_back(bl); indices.push_back(br);
                    min_y = std::min({min_y, vertices[tl].position.y, vertices[tr].position.y,
                                      vertices[bl].position.y, vertices[br].position.y});
                    max_y = std::max({max_y, vertices[tl].position.y, vertices[tr].position.y,
                                      vertices[bl].position.y, vertices[br].position.y});
                }
            }
            tile.index_count = static_cast<uint32_t>(indices.size()) - tile.first_index;
            tile.aabb_min = glm::vec3(start_x + cx0 * kShadowCellSize, min_y,
                                      start_z + cz0 * kShadowCellSize);
            tile.aabb_max = glm::vec3(start_x + cx1 * kShadowCellSize, max_y,
                                      start_z + cz1 * kShadowCellSize);
            if (tile.index_count > 0) shadow_tiles_.push_back(tile);
        }
    }

    shadow_vertex_buffer_ = gpu::GPUBuffer::create_static(
        *device_, gpu::GPUBuffer::Type::Vertex,
        vertices.data(), vertices.size() * sizeof(TerrainVertex));
    shadow_index_buffer_ = gpu::GPUBuffer::create_static(
        *device_, gpu::GPUBuffer::Type::Index,
        indices.data(), indices.size() * sizeof(uint32_t));
    shadow_index_count_ = static_cast<uint32_t>(indices.size());
    SDL_Log("TerrainRenderer: Shadow mesh %d cells / %zu tiles -> %u triangles (main: %u tris, %zu tiles)",
            cells_x * cells_z, shadow_tiles_.size(),
            shadow_index_count_ / 3, index_count_ / 3, tiles_.size());
}

} // namespace mmo::engine::render
