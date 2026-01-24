#include "terrain_renderer.hpp"
#include <cmath>
#include <vector>
#include <iostream>
#include <fstream>
#include <SDL3_image/SDL_image.h>
#include <glm/gtc/type_ptr.hpp>

namespace mmo {

namespace {

// Terrain vertex structure
struct TerrainVertex {
    float x, y, z;    // Position
    float u, v;       // UV
    float r, g, b, a; // Color
};

// Load shader binary file
bgfx::ShaderHandle load_shader(const char* name) {
    std::string path = std::string("shaders/") + name + ".bin";
    
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::cerr << "Failed to open shader: " << path << std::endl;
        return BGFX_INVALID_HANDLE;
    }
    
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    
    const bgfx::Memory* mem = bgfx::alloc(static_cast<uint32_t>(size + 1));
    if (!file.read(reinterpret_cast<char*>(mem->data), size)) {
        std::cerr << "Failed to read shader: " << path << std::endl;
        return BGFX_INVALID_HANDLE;
    }
    mem->data[size] = '\0';
    
    return bgfx::createShader(mem);
}

} // anonymous namespace

TerrainRenderer::TerrainRenderer() = default;

TerrainRenderer::~TerrainRenderer() {
    shutdown();
}

bool TerrainRenderer::init(float world_width, float world_height) {
    world_width_ = world_width;
    world_height_ = world_height;
    
    // Initialize vertex layout
    layout_
        .begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Float, true)
        .end();
    
    // Note: u_viewProj and u_model are bgfx predefined uniforms - use setViewTransform/setTransform
    // Create only custom uniforms
    u_cameraPos_ = bgfx::createUniform("u_cameraPos", bgfx::UniformType::Vec4);
    u_fogParams_ = bgfx::createUniform("u_fogParams", bgfx::UniformType::Vec4);
    u_fogParams2_ = bgfx::createUniform("u_fogParams2", bgfx::UniformType::Vec4);
    u_lightSpaceMatrix_ = bgfx::createUniform("u_lightSpaceMatrix", bgfx::UniformType::Mat4);
    u_lightDir_ = bgfx::createUniform("u_lightDir", bgfx::UniformType::Vec4);
    u_screenParams_ = bgfx::createUniform("u_screenParams", bgfx::UniformType::Vec4);
    s_grassTexture_ = bgfx::createUniform("s_grassTexture", bgfx::UniformType::Sampler);
    s_shadowMap_ = bgfx::createUniform("s_shadowMap", bgfx::UniformType::Sampler);
    s_ssaoTexture_ = bgfx::createUniform("s_ssaoTexture", bgfx::UniformType::Sampler);
    
    load_shaders();
    load_grass_texture();
    generate_terrain_mesh();
    
    return bgfx::isValid(program_);
}

void TerrainRenderer::load_shaders() {
    bgfx::ShaderHandle vs = load_shader("terrain_vs");
    bgfx::ShaderHandle fs = load_shader("terrain_fs");
    
    if (bgfx::isValid(vs) && bgfx::isValid(fs)) {
        program_ = bgfx::createProgram(vs, fs, true);
        std::cout << "Loaded terrain shaders" << std::endl;
    } else {
        std::cerr << "Failed to load terrain shaders" << std::endl;
        if (bgfx::isValid(vs)) bgfx::destroy(vs);
        if (bgfx::isValid(fs)) bgfx::destroy(fs);
    }
}

void TerrainRenderer::shutdown() {
    if (bgfx::isValid(vbh_)) {
        bgfx::destroy(vbh_);
        vbh_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(ibh_)) {
        bgfx::destroy(ibh_);
        ibh_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(grass_texture_)) {
        bgfx::destroy(grass_texture_);
        grass_texture_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(program_)) {
        bgfx::destroy(program_);
        program_ = BGFX_INVALID_HANDLE;
    }
    
    // Destroy uniforms (Note: u_viewProj is bgfx predefined, not created by us)
    if (bgfx::isValid(u_cameraPos_)) bgfx::destroy(u_cameraPos_);
    if (bgfx::isValid(u_fogParams_)) bgfx::destroy(u_fogParams_);
    if (bgfx::isValid(u_fogParams2_)) bgfx::destroy(u_fogParams2_);
    if (bgfx::isValid(u_lightSpaceMatrix_)) bgfx::destroy(u_lightSpaceMatrix_);
    if (bgfx::isValid(u_lightDir_)) bgfx::destroy(u_lightDir_);
    if (bgfx::isValid(u_screenParams_)) bgfx::destroy(u_screenParams_);
    if (bgfx::isValid(s_grassTexture_)) bgfx::destroy(s_grassTexture_);
    if (bgfx::isValid(s_shadowMap_)) bgfx::destroy(s_shadowMap_);
    if (bgfx::isValid(s_ssaoTexture_)) bgfx::destroy(s_ssaoTexture_);
    
    // Reset handles (Note: u_viewProj is bgfx predefined)
    u_cameraPos_ = BGFX_INVALID_HANDLE;
    u_fogParams_ = BGFX_INVALID_HANDLE;
    u_fogParams2_ = BGFX_INVALID_HANDLE;
    u_lightSpaceMatrix_ = BGFX_INVALID_HANDLE;
    u_lightDir_ = BGFX_INVALID_HANDLE;
    u_screenParams_ = BGFX_INVALID_HANDLE;
    s_grassTexture_ = BGFX_INVALID_HANDLE;
    s_shadowMap_ = BGFX_INVALID_HANDLE;
    s_ssaoTexture_ = BGFX_INVALID_HANDLE;
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
            
            const bgfx::Memory* mem = bgfx::copy(
                rgba_surface->pixels,
                static_cast<uint32_t>(tex_width * tex_height * 4)
            );
            
            grass_texture_ = bgfx::createTexture2D(
                static_cast<uint16_t>(tex_width),
                static_cast<uint16_t>(tex_height),
                true, 1,  // hasMips, numLayers
                bgfx::TextureFormat::RGBA8,
                BGFX_SAMPLER_U_MIRROR | BGFX_SAMPLER_V_MIRROR | BGFX_SAMPLER_MIN_ANISOTROPIC,
                mem
            );
            
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
    
    std::vector<TerrainVertex> vertices;
    std::vector<uint32_t> indices;
    
    // Generate vertices: x, y, z, u, v, r, g, b, a
    for (int iz = 0; iz <= cells_z; ++iz) {
        for (int ix = 0; ix <= cells_x; ++ix) {
            TerrainVertex v;
            v.x = start_x + ix * cell_size;
            v.z = start_z + iz * cell_size;
            v.y = get_height(v.x, v.z);
            v.u = v.x * tex_scale;
            v.v = v.z * tex_scale;
            
            // Color tint based on position
            float world_center_x = world_width_ / 2.0f;
            float world_center_z = world_height_ / 2.0f;
            float dx = v.x - world_center_x;
            float dz = v.z - world_center_z;
            float dist = std::sqrt(dx * dx + dz * dz);
            
            float dist_factor = std::min(dist / 3000.0f, 1.0f);
            float height_factor = std::min(std::max(v.y / 100.0f, 0.0f), 1.0f);
            
            v.r = 0.95f + dist_factor * 0.05f;
            v.g = 1.0f - dist_factor * 0.05f - height_factor * 0.05f;
            v.b = 0.9f + dist_factor * 0.05f;
            v.a = 1.0f;
            
            vertices.push_back(v);
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
    const bgfx::Memory* vb_mem = bgfx::copy(
        vertices.data(),
        static_cast<uint32_t>(vertices.size() * sizeof(TerrainVertex))
    );
    vbh_ = bgfx::createVertexBuffer(vb_mem, layout_);
    
    // Create index buffer
    const bgfx::Memory* ib_mem = bgfx::copy(
        indices.data(),
        static_cast<uint32_t>(indices.size() * sizeof(uint32_t))
    );
    ibh_ = bgfx::createIndexBuffer(ib_mem, BGFX_BUFFER_INDEX32);
    
    std::cout << "Generated terrain mesh with " << index_count_ << " indices" << std::endl;
}

void TerrainRenderer::render(bgfx::ViewId view_id, const glm::mat4& view, const glm::mat4& projection,
                             const glm::vec3& camera_pos, const glm::mat4& light_space_matrix,
                             bgfx::TextureHandle shadow_map, bool shadows_enabled,
                             bgfx::TextureHandle ssao_texture, bool ssao_enabled,
                             const glm::vec3& light_dir, const glm::vec2& screen_size) {
    if (!bgfx::isValid(program_) || !bgfx::isValid(vbh_)) return;
    
    // Set view and projection matrices using bgfx's predefined uniforms
    bgfx::setViewTransform(view_id, glm::value_ptr(view), glm::value_ptr(projection));
    
    // Camera position (vec4 for padding)
    float cam_pos[4] = { camera_pos.x, camera_pos.y, camera_pos.z, 0.0f };
    bgfx::setUniform(u_cameraPos_, cam_pos);
    
    // Fog settings
    float fog_params[4] = { fog_color_.r, fog_color_.g, fog_color_.b, fog_start_ };
    bgfx::setUniform(u_fogParams_, fog_params);
    float fog_params2[4] = { fog_end_, 0.0f, 0.0f, 0.0f };
    bgfx::setUniform(u_fogParams2_, fog_params2);
    
    // Light space matrix
    bgfx::setUniform(u_lightSpaceMatrix_, glm::value_ptr(light_space_matrix));
    
    // Light direction
    float light_dir_v[4] = { light_dir.x, light_dir.y, light_dir.z, 0.0f };
    bgfx::setUniform(u_lightDir_, light_dir_v);
    
    // Screen params: screenSize.xy, shadowsEnabled, ssaoEnabled
    float screen_params[4] = { screen_size.x, screen_size.y, shadows_enabled ? 1.0f : 0.0f, ssao_enabled ? 1.0f : 0.0f };
    bgfx::setUniform(u_screenParams_, screen_params);
    
    // Bind textures
    bgfx::setTexture(0, s_grassTexture_, grass_texture_);
    if (bgfx::isValid(shadow_map)) {
        bgfx::setTexture(1, s_shadowMap_, shadow_map);
    }
    if (bgfx::isValid(ssao_texture)) {
        bgfx::setTexture(2, s_ssaoTexture_, ssao_texture);
    }
    
    // Set render state
    uint64_t state = BGFX_STATE_WRITE_RGB
                   | BGFX_STATE_WRITE_A
                   | BGFX_STATE_WRITE_Z
                   | BGFX_STATE_DEPTH_TEST_LESS
                   | BGFX_STATE_CULL_CCW
                   | BGFX_STATE_MSAA;
    bgfx::setState(state);
    
    // Submit geometry
    bgfx::setVertexBuffer(0, vbh_);
    bgfx::setIndexBuffer(ibh_);
    bgfx::submit(view_id, program_);
}

} // namespace mmo
