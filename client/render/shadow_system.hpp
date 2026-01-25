#pragma once

#include "../shader.hpp"
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace mmo {

/**
 * ShadowSystem manages shadow mapping:
 * - Shadow framebuffer and depth texture
 * - Light space matrix calculation
 * - Shadow pass management
 */
class ShadowSystem {
public:
    ShadowSystem();
    ~ShadowSystem();
    
    // Non-copyable
    ShadowSystem(const ShadowSystem&) = delete;
    ShadowSystem& operator=(const ShadowSystem&) = delete;
    
    /**
     * Initialize shadow mapping resources.
     * @param shadow_map_size Resolution of the shadow map (e.g., 4096)
     */
    bool init(int shadow_map_size = 4096);
    
    /**
     * Clean up resources.
     */
    void shutdown();
    
    /**
     * Update light space matrix based on camera position.
     */
    void update_light_space_matrix(float camera_x, float camera_z, const glm::vec3& light_dir);

    /**
     * Begin shadow pass - bind shadow FBO and set up state.
     */
    void begin_shadow_pass();
    
    /**
     * End shadow pass - restore state.
     */
    void end_shadow_pass();
    
    /**
     * Get shadow depth shader for rendering to shadow map.
     */
    Shader* shadow_shader() const { return shadow_depth_shader_.get(); }
    Shader* skinned_shadow_shader() const { return skinned_shadow_depth_shader_.get(); }
    
    // Accessors
    GLuint shadow_depth_texture() const { return shadow_depth_texture_; }
    const glm::mat4& light_space_matrix() const { return light_space_matrix_; }
    bool is_enabled() const { return enabled_; }
    void set_enabled(bool enabled) { enabled_ = enabled; }
    
private:
    static constexpr int DEFAULT_SHADOW_MAP_SIZE = 4096;
    
    bool enabled_ = true;
    int shadow_map_size_ = DEFAULT_SHADOW_MAP_SIZE;
    
    GLuint shadow_fbo_ = 0;
    GLuint shadow_depth_texture_ = 0;
    
    glm::mat4 light_space_matrix_;
    
    std::unique_ptr<Shader> shadow_depth_shader_;
    std::unique_ptr<Shader> skinned_shadow_depth_shader_;
};

/**
 * SSAOSystem manages Screen-Space Ambient Occlusion:
 * - G-buffer for position/normal
 * - SSAO framebuffer and blur
 * - Kernel generation
 */
class SSAOSystem {
public:
    SSAOSystem();
    ~SSAOSystem();
    
    // Non-copyable
    SSAOSystem(const SSAOSystem&) = delete;
    SSAOSystem& operator=(const SSAOSystem&) = delete;
    
    /**
     * Initialize SSAO resources.
     * @param width Screen width
     * @param height Screen height
     */
    bool init(int width, int height);
    
    /**
     * Clean up resources.
     */
    void shutdown();
    
    /**
     * Resize SSAO buffers.
     */
    void resize(int width, int height);
    
    /**
     * Get the final blurred SSAO texture.
     */
    GLuint ssao_texture() const { return ssao_blur_buffer_; }
    
    // Accessors
    bool is_enabled() const { return enabled_; }
    void set_enabled(bool enabled) { enabled_ = enabled; }
    
    // G-buffer accessors for deferred shading
    GLuint gbuffer_fbo() const { return gbuffer_fbo_; }
    GLuint gbuffer_position() const { return gbuffer_position_; }
    GLuint gbuffer_normal() const { return gbuffer_normal_; }
    
    // SSAO kernel for shader
    const std::vector<glm::vec3>& kernel() const { return ssao_kernel_; }
    GLuint noise_texture() const { return ssao_noise_texture_; }
    
    // Screen quad for post-processing
    GLuint screen_quad_vao() const { return screen_quad_vao_; }
    
    // Shaders
    Shader* ssao_shader() const { return ssao_shader_.get(); }
    Shader* ssao_blur_shader() const { return ssao_blur_shader_.get(); }
    Shader* gbuffer_shader() const { return gbuffer_shader_.get(); }
    
private:
    void generate_kernel();
    void create_framebuffers(int width, int height);
    void create_screen_quad();
    
    bool enabled_ = true;
    int width_ = 0;
    int height_ = 0;
    
    GLuint gbuffer_fbo_ = 0;
    GLuint gbuffer_position_ = 0;
    GLuint gbuffer_normal_ = 0;
    GLuint gbuffer_depth_ = 0;
    
    GLuint ssao_fbo_ = 0;
    GLuint ssao_blur_fbo_ = 0;
    GLuint ssao_color_buffer_ = 0;
    GLuint ssao_blur_buffer_ = 0;
    GLuint ssao_noise_texture_ = 0;
    
    GLuint screen_quad_vao_ = 0;
    GLuint screen_quad_vbo_ = 0;
    
    std::vector<glm::vec3> ssao_kernel_;
    
    std::unique_ptr<Shader> ssao_shader_;
    std::unique_ptr<Shader> ssao_blur_shader_;
    std::unique_ptr<Shader> gbuffer_shader_;
};

} // namespace mmo
