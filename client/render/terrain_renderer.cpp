#include "terrain_renderer.hpp"
#include "../shader.hpp"
#include <cmath>
#include <vector>
#include <iostream>
#include <SDL3_image/SDL_image.h>

namespace mmo {

TerrainRenderer::TerrainRenderer() = default;

TerrainRenderer::~TerrainRenderer() {
    shutdown();
}

bool TerrainRenderer::init(float world_width, float world_height) {
    world_width_ = world_width;
    world_height_ = world_height;
    
    // Create terrain shader
    terrain_shader_ = std::make_unique<Shader>();
    if (!terrain_shader_->load(shaders::terrain_vertex, shaders::terrain_fragment)) {
        std::cerr << "Failed to load terrain shader" << std::endl;
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
    if (!heightmap_) return;
    
    // Delete old texture if exists
    if (heightmap_texture_) {
        glDeleteTextures(1, &heightmap_texture_);
    }
    
    // Create R16 texture (16-bit height values)
    glGenTextures(1, &heightmap_texture_);
    glBindTexture(GL_TEXTURE_2D, heightmap_texture_);
    
    // Upload as R16 (unsigned normalized - values 0-65535 map to 0.0-1.0)
    // Note: Data layout is height_data[z * res + x], uploaded row by row.
    // OpenGL's texelFetch(ivec2(x, y), 0) reads from the same layout.
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R16, 
                 heightmap_->resolution, heightmap_->resolution, 
                 0, GL_RED, GL_UNSIGNED_SHORT, 
                 heightmap_->height_data.data());
    
    // Nearest filtering - we do manual bilinear interpolation in shaders for exact CPU match
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    
    // Clamp to edge to avoid wrapping artifacts
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    glBindTexture(GL_TEXTURE_2D, 0);
    
}

void TerrainRenderer::shutdown() {
    if (vao_) {
        glDeleteVertexArrays(1, &vao_);
        vao_ = 0;
    }
    if (vbo_) {
        glDeleteBuffers(1, &vbo_);
        vbo_ = 0;
    }
    if (ibo_) {
        glDeleteBuffers(1, &ibo_);
        ibo_ = 0;
    }
    if (grass_texture_) {
        glDeleteTextures(1, &grass_texture_);
        grass_texture_ = 0;
    }
    if (heightmap_texture_) {
        glDeleteTextures(1, &heightmap_texture_);
        heightmap_texture_ = 0;
    }
    heightmap_.reset();
    terrain_shader_.reset();
}

void TerrainRenderer::load_grass_texture() {
    SDL_Surface* surface = IMG_Load("assets/textures/grass_seamless.png");
    if (surface) {
        // Convert to RGBA format if needed
        SDL_Surface* rgba_surface = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);
        SDL_DestroySurface(surface);
        
        if (rgba_surface) {
            int tex_width = rgba_surface->w;
            int tex_height = rgba_surface->h;
            
            glGenTextures(1, &grass_texture_);
            glBindTexture(GL_TEXTURE_2D, grass_texture_);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tex_width, tex_height, 0, 
                         GL_RGBA, GL_UNSIGNED_BYTE, rgba_surface->pixels);
            glGenerateMipmap(GL_TEXTURE_2D);
            
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            
            float max_aniso;
            glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &max_aniso);
            glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, std::min(max_aniso, 8.0f));
            
            SDL_DestroySurface(rgba_surface);
        } else {
            std::cerr << "Failed to convert grass texture to RGBA" << std::endl;
        }
    } else {
        std::cerr << "Failed to load grass texture: " << SDL_GetError() << std::endl;
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
    float margin = 5000.0f;
    float start_x = -margin;
    float start_z = -margin;
    float end_x = world_width_ + margin;
    float end_z = world_height_ + margin;
    
    float cell_size = 25.0f;
    int cells_x = static_cast<int>((end_x - start_x) / cell_size);
    int cells_z = static_cast<int>((end_z - start_z) / cell_size);
    
    float tex_scale = 0.01f;
    
    std::vector<float> vertices;
    std::vector<uint32_t> indices;
    
    // Generate vertices: x, y, z, u, v, r, g, b, a
    for (int iz = 0; iz <= cells_z; ++iz) {
        for (int ix = 0; ix <= cells_x; ++ix) {
            float x = start_x + ix * cell_size;
            float z = start_z + iz * cell_size;
            float y = get_height(x, z);
            
            vertices.push_back(x);
            vertices.push_back(y);
            vertices.push_back(z);
            vertices.push_back(x * tex_scale);
            vertices.push_back(z * tex_scale);
            
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
            
            vertices.push_back(r);
            vertices.push_back(g);
            vertices.push_back(b);
            vertices.push_back(1.0f);
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
    
    vertex_count_ = static_cast<GLuint>(indices.size());
    
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glGenBuffers(1, &ibo_);
    
    glBindVertexArray(vao_);
    
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), 
                 vertices.data(), GL_STATIC_DRAW);
    
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(uint32_t), 
                 indices.data(), GL_STATIC_DRAW);
    
    // Position (3 floats)
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    
    // UV (2 floats)
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    
    // Color (4 floats)
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)(5 * sizeof(float)));
    glEnableVertexAttribArray(2);
    
    glBindVertexArray(0);
}

void TerrainRenderer::render(const glm::mat4& view, const glm::mat4& projection,
                             const glm::vec3& camera_pos, const glm::mat4& light_space_matrix,
                             GLuint shadow_map, bool shadows_enabled,
                             GLuint ssao_texture, bool ssao_enabled,
                             const glm::vec3& light_dir, const glm::vec2& screen_size) {
    if (!terrain_shader_ || !vao_) return;
    
    terrain_shader_->use();
    terrain_shader_->set_mat4("view", view);
    terrain_shader_->set_mat4("projection", projection);
    terrain_shader_->set_vec3("cameraPos", camera_pos);
    
    // Fog settings
    terrain_shader_->set_vec3("fogColor", fog_color_);
    terrain_shader_->set_float("fogStart", fog_start_);
    terrain_shader_->set_float("fogEnd", fog_end_);
    
    // Shadow mapping
    terrain_shader_->set_mat4("lightSpaceMatrix", light_space_matrix);
    terrain_shader_->set_int("shadowsEnabled", shadows_enabled ? 1 : 0);
    terrain_shader_->set_vec3("lightDir", light_dir);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, shadow_map);
    terrain_shader_->set_int("shadowMap", 1);
    
    // SSAO
    terrain_shader_->set_int("ssaoEnabled", ssao_enabled ? 1 : 0);
    terrain_shader_->set_vec2("screenSize", screen_size);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, ssao_texture);
    terrain_shader_->set_int("ssaoTexture", 2);
    
    // Grass texture
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, grass_texture_);
    terrain_shader_->set_int("grassTexture", 0);
    
    glBindVertexArray(vao_);
    glDrawElements(GL_TRIANGLES, vertex_count_, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
}

void TerrainRenderer::set_anisotropic_filter(float level) {
    if (grass_texture_ != 0) {
        glBindTexture(GL_TEXTURE_2D, grass_texture_);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, level);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
}

} // namespace mmo
