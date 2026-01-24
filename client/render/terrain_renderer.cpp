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
    generate_terrain_mesh();
    
    return true;
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
            std::cout << "Loaded grass texture: " << tex_width << "x" << tex_height << std::endl;
        } else {
            std::cerr << "Failed to convert grass texture to RGBA" << std::endl;
        }
    } else {
        std::cerr << "Failed to load grass texture: " << SDL_GetError() << std::endl;
    }
}

float TerrainRenderer::get_height(float x, float z) const {
    float world_center_x = world_width_ / 2.0f;
    float world_center_z = world_height_ / 2.0f;
    
    float dx = x - world_center_x;
    float dz = z - world_center_z;
    float dist = std::sqrt(dx * dx + dz * dz);
    
    // Keep playable area relatively flat
    float playable_radius = 600.0f;
    float transition_radius = 400.0f;
    float flatness = 1.0f;
    
    if (dist < playable_radius) {
        flatness = 0.1f;
    } else if (dist < playable_radius + transition_radius) {
        float t = (dist - playable_radius) / transition_radius;
        flatness = 0.1f + t * 0.9f;
    }
    
    // Multi-octave noise for natural terrain
    float height = 0.0f;
    
    // Large rolling hills
    float freq1 = 0.0008f;
    height += std::sin(x * freq1 * 1.1f) * std::cos(z * freq1 * 0.9f) * 80.0f;
    height += std::sin(x * freq1 * 0.7f + 1.3f) * std::sin(z * freq1 * 1.2f + 0.7f) * 60.0f;
    
    // Medium undulations
    float freq2 = 0.003f;
    height += std::sin(x * freq2 * 1.3f + 2.1f) * std::cos(z * freq2 * 0.8f + 1.4f) * 25.0f;
    height += std::cos(x * freq2 * 0.9f) * std::sin(z * freq2 * 1.1f + 0.5f) * 20.0f;
    
    // Small bumps
    float freq3 = 0.01f;
    height += std::sin(x * freq3 * 1.7f + 0.3f) * std::cos(z * freq3 * 1.4f + 2.1f) * 8.0f;
    height += std::cos(x * freq3 * 1.2f + 1.8f) * std::sin(z * freq3 * 0.9f) * 6.0f;
    
    height *= flatness;
    
    // Terrain rises toward mountains
    if (dist > 2000.0f) {
        float rise_factor = (dist - 2000.0f) / 2000.0f;
        rise_factor = std::min(rise_factor, 1.0f);
        height += rise_factor * rise_factor * 150.0f;
    }
    
    return height;
}

glm::vec3 TerrainRenderer::get_normal(float x, float z) const {
    float eps = 5.0f;
    float hL = get_height(x - eps, z);
    float hR = get_height(x + eps, z);
    float hD = get_height(x, z - eps);
    float hU = get_height(x, z + eps);
    
    glm::vec3 normal(hL - hR, 2.0f * eps, hD - hU);
    return glm::normalize(normal);
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
    
    std::cout << "Generated terrain mesh with " << vertex_count_ << " indices" << std::endl;
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

} // namespace mmo
