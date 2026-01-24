#pragma once

#include "render_context.hpp"
#include <bgfx/bgfx.h>
#include <glm/glm.hpp>
#include <vector>

namespace mmo {

/**
 * ShadowSystem manages shadow mapping using bgfx:
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
     * Begin shadow pass - set up view and framebuffer.
     */
    void begin_shadow_pass();
    
    /**
     * End shadow pass.
     */
    void end_shadow_pass();
    
    /**
     * Get shadow program for rendering to shadow map.
     */
    bgfx::ProgramHandle shadow_program() const { return shadow_program_; }
    bgfx::ProgramHandle skinned_shadow_program() const { return skinned_shadow_program_; }
    
    // Accessors
    bgfx::TextureHandle shadow_depth_texture() const { return shadow_depth_texture_; }
    const glm::mat4& light_space_matrix() const { return light_space_matrix_; }
    bool is_enabled() const { return enabled_; }
    void set_enabled(bool enabled) { enabled_ = enabled; }
    
    // Uniform handle for light space matrix
    bgfx::UniformHandle u_lightSpaceMatrix() const { return u_lightSpaceMatrix_; }
    bgfx::UniformHandle u_model() const { return u_model_; }
    
private:
    static constexpr int DEFAULT_SHADOW_MAP_SIZE = 4096;
    
    bool enabled_ = true;
    int shadow_map_size_ = DEFAULT_SHADOW_MAP_SIZE;
    
    bgfx::FrameBufferHandle shadow_fbo_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle shadow_depth_texture_ = BGFX_INVALID_HANDLE;
    
    bgfx::ProgramHandle shadow_program_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle skinned_shadow_program_ = BGFX_INVALID_HANDLE;
    
    bgfx::UniformHandle u_lightSpaceMatrix_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_model_ = BGFX_INVALID_HANDLE;
    
    glm::mat4 light_space_matrix_;
};

/**
 * SSAOSystem manages Screen-Space Ambient Occlusion using bgfx:
 * - G-buffer for position/normal
 * - SSAO framebuffer and blur
 * - Kernel generation
 * 
 * Note: For simplicity, SSAO is disabled in the bgfx migration.
 * It can be added back later with proper framebuffer management.
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
    bgfx::TextureHandle ssao_texture() const { return ssao_blur_texture_; }
    
    // Accessors
    bool is_enabled() const { return enabled_; }
    void set_enabled(bool enabled) { enabled_ = enabled; }
    
private:
    bool enabled_ = false;  // Disabled for now in bgfx migration
    int width_ = 0;
    int height_ = 0;
    
    bgfx::TextureHandle ssao_blur_texture_ = BGFX_INVALID_HANDLE;
};

} // namespace mmo
