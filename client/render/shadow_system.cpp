#include "shadow_system.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <cstdlib>

namespace mmo {

// ============================================================================
// ShadowSystem
// ============================================================================

ShadowSystem::ShadowSystem() = default;

ShadowSystem::~ShadowSystem() {
    shutdown();
}

bool ShadowSystem::init(int shadow_map_size) {
    shadow_map_size_ = shadow_map_size;
    
    std::cout << "Initializing shadow mapping..." << std::endl;
    
    // Create shadow depth texture
    glGenTextures(1, &shadow_depth_texture_);
    glBindTexture(GL_TEXTURE_2D, shadow_depth_texture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, shadow_map_size_, shadow_map_size_,
                 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    float borderColor[] = {1.0f, 1.0f, 1.0f, 1.0f};
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);
    
    // Create shadow framebuffer
    glGenFramebuffers(1, &shadow_fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, shadow_fbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadow_depth_texture_, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "Shadow framebuffer not complete!" << std::endl;
        return false;
    }
    
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    
    // Create shadow depth shaders
    shadow_depth_shader_ = std::make_unique<Shader>();
    if (!shadow_depth_shader_->load(shaders::shadow_depth_vertex, shaders::shadow_depth_fragment)) {
        std::cerr << "Failed to load shadow depth shader" << std::endl;
        return false;
    }
    
    skinned_shadow_depth_shader_ = std::make_unique<Shader>();
    if (!skinned_shadow_depth_shader_->load(shaders::skinned_shadow_depth_vertex, shaders::shadow_depth_fragment)) {
        std::cerr << "Failed to load skinned shadow depth shader" << std::endl;
        return false;
    }
    
    std::cout << "Shadow mapping initialized with " << shadow_map_size_ << "x" << shadow_map_size_ << " shadow map" << std::endl;
    return true;
}

void ShadowSystem::shutdown() {
    if (shadow_fbo_) {
        glDeleteFramebuffers(1, &shadow_fbo_);
        shadow_fbo_ = 0;
    }
    if (shadow_depth_texture_) {
        glDeleteTextures(1, &shadow_depth_texture_);
        shadow_depth_texture_ = 0;
    }
    shadow_depth_shader_.reset();
    skinned_shadow_depth_shader_.reset();
}

void ShadowSystem::update_light_space_matrix(float camera_x, float camera_z, const glm::vec3& light_dir) {
    float shadow_distance = 1500.0f;
    
    // Use known terrain height bounds from heightmap config (-500 to 500)
    // Plus margin for objects on terrain
    constexpr float MIN_TERRAIN = -500.0f - 100.0f;   // Below terrain
    constexpr float MAX_TERRAIN = 500.0f + 600.0f;    // Above for trees/buildings
    
    float center_height = (MIN_TERRAIN + MAX_TERRAIN) * 0.5f;
    float height_range = MAX_TERRAIN - MIN_TERRAIN;
    
    // Position light to look at the center of the shadow volume
    glm::vec3 center = glm::vec3(camera_x, center_height, camera_z);
    glm::vec3 light_pos = center - light_dir * (shadow_distance + height_range);
    glm::mat4 light_view = glm::lookAt(light_pos, center, glm::vec3(0.0f, 1.0f, 0.0f));
    
    // Orthographic projection covering the full height range
    float near_plane = 1.0f;
    float far_plane = 2.0f * (shadow_distance + height_range) + height_range;
    
    glm::mat4 light_projection = glm::ortho(
        -shadow_distance, shadow_distance,
        -shadow_distance, shadow_distance,
        near_plane, far_plane
    );
    
    light_space_matrix_ = light_projection * light_view;
}

void ShadowSystem::begin_shadow_pass() {
    if (!enabled_ || !shadow_depth_shader_) return;
    
    glBindFramebuffer(GL_FRAMEBUFFER, shadow_fbo_);
    glViewport(0, 0, shadow_map_size_, shadow_map_size_);
    glClear(GL_DEPTH_BUFFER_BIT);
    
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_FRONT);  // Reduce shadow acne
    
    shadow_depth_shader_->use();
    shadow_depth_shader_->set_mat4("lightSpaceMatrix", light_space_matrix_);
}

void ShadowSystem::end_shadow_pass() {
    if (!enabled_) return;
    
    glCullFace(GL_BACK);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// ============================================================================
// SSAOSystem
// ============================================================================

SSAOSystem::SSAOSystem() = default;

SSAOSystem::~SSAOSystem() {
    shutdown();
}

bool SSAOSystem::init(int width, int height) {
    width_ = width;
    height_ = height;
    
    std::cout << "Initializing SSAO..." << std::endl;
    
    // Generate kernel
    generate_kernel();
    
    // Generate noise texture
    std::vector<glm::vec3> ssao_noise;
    for (int i = 0; i < 16; ++i) {
        glm::vec3 noise(
            static_cast<float>(rand()) / RAND_MAX * 2.0f - 1.0f,
            static_cast<float>(rand()) / RAND_MAX * 2.0f - 1.0f,
            0.0f
        );
        ssao_noise.push_back(noise);
    }
    
    glGenTextures(1, &ssao_noise_texture_);
    glBindTexture(GL_TEXTURE_2D, ssao_noise_texture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, 4, 4, 0, GL_RGB, GL_FLOAT, ssao_noise.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    
    // Create framebuffers
    create_framebuffers(width, height);
    
    // Create screen quad
    create_screen_quad();
    
    // Create shaders
    ssao_shader_ = std::make_unique<Shader>();
    if (!ssao_shader_->load(shaders::ssao_vertex, shaders::ssao_fragment)) {
        std::cerr << "Failed to load SSAO shader" << std::endl;
        return false;
    }
    
    ssao_blur_shader_ = std::make_unique<Shader>();
    if (!ssao_blur_shader_->load(shaders::ssao_vertex, shaders::ssao_blur_fragment)) {
        std::cerr << "Failed to load SSAO blur shader" << std::endl;
        return false;
    }
    
    gbuffer_shader_ = std::make_unique<Shader>();
    if (!gbuffer_shader_->load(shaders::ssao_gbuffer_vertex, shaders::ssao_gbuffer_fragment)) {
        std::cerr << "Failed to load G-buffer shader" << std::endl;
        return false;
    }
    
    std::cout << "SSAO initialized" << std::endl;
    return true;
}

void SSAOSystem::shutdown() {
    if (gbuffer_fbo_) glDeleteFramebuffers(1, &gbuffer_fbo_);
    if (ssao_fbo_) glDeleteFramebuffers(1, &ssao_fbo_);
    if (ssao_blur_fbo_) glDeleteFramebuffers(1, &ssao_blur_fbo_);
    
    if (gbuffer_position_) glDeleteTextures(1, &gbuffer_position_);
    if (gbuffer_normal_) glDeleteTextures(1, &gbuffer_normal_);
    if (gbuffer_depth_) glDeleteTextures(1, &gbuffer_depth_);
    if (ssao_color_buffer_) glDeleteTextures(1, &ssao_color_buffer_);
    if (ssao_blur_buffer_) glDeleteTextures(1, &ssao_blur_buffer_);
    if (ssao_noise_texture_) glDeleteTextures(1, &ssao_noise_texture_);
    
    if (screen_quad_vao_) glDeleteVertexArrays(1, &screen_quad_vao_);
    if (screen_quad_vbo_) glDeleteBuffers(1, &screen_quad_vbo_);
    
    gbuffer_fbo_ = ssao_fbo_ = ssao_blur_fbo_ = 0;
    gbuffer_position_ = gbuffer_normal_ = gbuffer_depth_ = 0;
    ssao_color_buffer_ = ssao_blur_buffer_ = ssao_noise_texture_ = 0;
    screen_quad_vao_ = screen_quad_vbo_ = 0;
    
    ssao_shader_.reset();
    ssao_blur_shader_.reset();
    gbuffer_shader_.reset();
}

void SSAOSystem::resize(int width, int height) {
    if (width == width_ && height == height_) return;
    
    // Delete old textures
    if (gbuffer_position_) glDeleteTextures(1, &gbuffer_position_);
    if (gbuffer_normal_) glDeleteTextures(1, &gbuffer_normal_);
    if (gbuffer_depth_) glDeleteTextures(1, &gbuffer_depth_);
    if (ssao_color_buffer_) glDeleteTextures(1, &ssao_color_buffer_);
    if (ssao_blur_buffer_) glDeleteTextures(1, &ssao_blur_buffer_);
    
    // Recreate framebuffers
    create_framebuffers(width, height);
}

void SSAOSystem::generate_kernel() {
    ssao_kernel_.clear();
    for (int i = 0; i < 64; ++i) {
        glm::vec3 sample(
            static_cast<float>(rand()) / RAND_MAX * 2.0f - 1.0f,
            static_cast<float>(rand()) / RAND_MAX * 2.0f - 1.0f,
            static_cast<float>(rand()) / RAND_MAX
        );
        sample = glm::normalize(sample);
        sample *= static_cast<float>(rand()) / RAND_MAX;
        
        float scale = static_cast<float>(i) / 64.0f;
        scale = 0.1f + scale * scale * 0.9f;
        sample *= scale;
        
        ssao_kernel_.push_back(sample);
    }
}

void SSAOSystem::create_framebuffers(int width, int height) {
    width_ = width;
    height_ = height;
    
    // G-buffer
    glGenFramebuffers(1, &gbuffer_fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, gbuffer_fbo_);
    
    // Position buffer
    glGenTextures(1, &gbuffer_position_);
    glBindTexture(GL_TEXTURE_2D, gbuffer_position_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, width, height, 0, GL_RGB, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gbuffer_position_, 0);
    
    // Normal buffer
    glGenTextures(1, &gbuffer_normal_);
    glBindTexture(GL_TEXTURE_2D, gbuffer_normal_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, width, height, 0, GL_RGB, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, gbuffer_normal_, 0);
    
    // Depth buffer
    glGenTextures(1, &gbuffer_depth_);
    glBindTexture(GL_TEXTURE_2D, gbuffer_depth_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, width, height, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, gbuffer_depth_, 0);
    
    unsigned int attachments[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
    glDrawBuffers(2, attachments);
    
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "G-buffer framebuffer not complete!" << std::endl;
    }
    
    // SSAO framebuffer
    glGenFramebuffers(1, &ssao_fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, ssao_fbo_);
    
    glGenTextures(1, &ssao_color_buffer_);
    glBindTexture(GL_TEXTURE_2D, ssao_color_buffer_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, width, height, 0, GL_RED, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, ssao_color_buffer_, 0);
    
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "SSAO framebuffer not complete!" << std::endl;
    }
    
    // SSAO blur framebuffer
    glGenFramebuffers(1, &ssao_blur_fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, ssao_blur_fbo_);
    
    glGenTextures(1, &ssao_blur_buffer_);
    glBindTexture(GL_TEXTURE_2D, ssao_blur_buffer_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, width, height, 0, GL_RED, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, ssao_blur_buffer_, 0);
    
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "SSAO blur framebuffer not complete!" << std::endl;
    }
    
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void SSAOSystem::create_screen_quad() {
    float quadVertices[] = {
        -1.0f,  1.0f,  0.0f, 1.0f,
        -1.0f, -1.0f,  0.0f, 0.0f,
         1.0f, -1.0f,  1.0f, 0.0f,
        -1.0f,  1.0f,  0.0f, 1.0f,
         1.0f, -1.0f,  1.0f, 0.0f,
         1.0f,  1.0f,  1.0f, 1.0f
    };
    
    glGenVertexArrays(1, &screen_quad_vao_);
    glGenBuffers(1, &screen_quad_vbo_);
    glBindVertexArray(screen_quad_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, screen_quad_vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
}

} // namespace mmo
