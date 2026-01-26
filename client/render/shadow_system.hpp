#pragma once

#include "../gpu/gpu_device.hpp"
#include "../gpu/gpu_texture.hpp"
#include "../gpu/gpu_buffer.hpp"
#include "../gpu/pipeline_registry.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>
#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace mmo {

// Forward declarations
namespace gpu {
    class GPUDevice;
    class GPUTexture;
    class GPUSampler;
    class PipelineRegistry;
    class GPUBuffer;
}

/**
 * ShadowSystem manages shadow mapping using SDL3 GPU API:
 * - Shadow depth texture (render target)
 * - Light space matrix calculation
 * - Shadow pass rendering
 * - Shadow map sampling setup
 * 
 * SDL3 GPU Migration: This system has been ported from OpenGL to use
 * SDL3 GPU depth textures and render passes. Shadow maps are rendered
 * using a depth-only render pass with front-face culling to reduce
 * shadow acne.
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
     * @param device GPU device for resource creation
     * @param pipeline_registry Pipeline registry for shadow pipelines
     * @param shadow_map_size Resolution of the shadow map (e.g., 4096)
     * @return true on success, false on failure
     */
    bool init(gpu::GPUDevice& device, gpu::PipelineRegistry& pipeline_registry, 
              int shadow_map_size = DEFAULT_SHADOW_MAP_SIZE);
    
    /**
     * Clean up resources.
     */
    void shutdown();
    
    /**
     * Update light space matrix based on camera position.
     * @param camera_x Camera X position for shadow frustum centering
     * @param camera_z Camera Z position for shadow frustum centering
     * @param light_dir Directional light direction (normalized)
     */
    void update_light_space_matrix(float camera_x, float camera_z, const glm::vec3& light_dir);
    
    /**
     * Begin shadow pass - create a depth-only render pass.
     * @param cmd Command buffer for rendering
     * @return Render pass for shadow map rendering, or nullptr on failure
     */
    SDL_GPURenderPass* begin_shadow_pass(SDL_GPUCommandBuffer* cmd);
    
    // Accessors
    const glm::mat4& light_space_matrix() const { return light_space_matrix_; }
    bool is_enabled() const { return enabled_; }
    void set_enabled(bool enabled) { enabled_ = enabled; }
    int shadow_map_size() const { return shadow_map_size_; }
    
    /**
     * End shadow pass.
     * @param pass The render pass to end
     */
    void end_shadow_pass(SDL_GPURenderPass* pass);
    
    /**
     * Render a static model to the shadow map.
     * @param pass The shadow render pass
     * @param cmd Command buffer
     * @param model_matrix The model's world transform
     * @param vertex_buffer The model's vertex buffer
    * @param index_buffer The model's index buffer (optional)
    * @param draw_count Number of indices (or vertex count if no index buffer)
     */
    void render_shadow_caster(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd,
                              const glm::mat4& model_matrix,
                              gpu::GPUBuffer* vertex_buffer,
                              gpu::GPUBuffer* index_buffer,
                        uint32_t draw_count);
    
    /**
     * Render a skinned model to the shadow map.
     * @param pass The shadow render pass
     * @param cmd Command buffer
     * @param model_matrix The model's world transform
     * @param bone_matrices Array of bone transform matrices
     * @param bone_count Number of bones
     * @param vertex_buffer The model's vertex buffer
    * @param index_buffer The model's index buffer (optional)
    * @param draw_count Number of indices
     */
    void render_skinned_shadow_caster(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd,
                                       const glm::mat4& model_matrix,
                                       const glm::mat4* bone_matrices, uint32_t bone_count,
                                       gpu::GPUBuffer* vertex_buffer,
                                       gpu::GPUBuffer* index_buffer,
                                uint32_t draw_count);
    
    // Accessors for backward compatibility during migration
    gpu::GPUTexture* shadow_map() const { return shadow_map_.get(); }
    SDL_GPUSampler* shadow_sampler() const { return shadow_sampler_; }
    
    static constexpr int DEFAULT_SHADOW_MAP_SIZE = 4096;
    
private:
    // Uniform structures for shadow pass
    struct alignas(16) ShadowUniforms {
        glm::mat4 light_space_matrix;
        glm::mat4 model;
    };
    
    static constexpr size_t MAX_BONES = 64;
    struct alignas(16) BoneUniforms {
        glm::mat4 bones[MAX_BONES];
    };
    
    bool enabled_ = true;
    int shadow_map_size_ = DEFAULT_SHADOW_MAP_SIZE;
    
    gpu::GPUDevice* device_ = nullptr;
    gpu::PipelineRegistry* pipeline_registry_ = nullptr;
    
    // Shadow map depth texture
    std::unique_ptr<gpu::GPUTexture> shadow_map_;
    
    // Shadow comparison sampler for PCF filtering
    SDL_GPUSampler* shadow_sampler_ = nullptr;
    
    // Light space matrix for shadow projection
    glm::mat4 light_space_matrix_ = glm::mat4(1.0f);
    
    // Shadow frustum parameters
    float shadow_distance_ = 1500.0f;
    
    // Cached pipeline pointers (owned by PipelineRegistry)
    gpu::GPUPipeline* shadow_pipeline_ = nullptr;
    gpu::GPUPipeline* skinned_shadow_pipeline_ = nullptr;
};

/**
 * SSAOSystem manages Screen-Space Ambient Occlusion using SDL3 GPU API:
 * - G-buffer for position/normal
 * - SSAO computation and blur passes
 * - Kernel generation
 * 
 * NOTE: SSAO is a complex post-processing effect. This system is
 * stubbed out for now and will be fully implemented in a future task
 * when the main renderer's deferred rendering pipeline is ready.
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
     * @param device GPU device for resource creation
     * @param width Screen width
     * @param height Screen height
     * @return true on success, false on failure
     */
    bool init(gpu::GPUDevice& device, int width, int height);
    
    /**
     * Clean up resources.
     */
    void shutdown();
    
    /**
     * Resize SSAO buffers.
     */
    void resize(int width, int height);
    
    // Accessors
    gpu::GPUTexture* ssao_texture() const { return ssao_blur_texture_.get(); }
    SDL_GPUSampler* ssao_sampler() const { return ssao_sampler_; }
    bool is_enabled() const { return enabled_; }
    void set_enabled(bool enabled) { enabled_ = enabled; }
    
private:
    void generate_kernel();
    
    bool enabled_ = false;  // Disabled by default until full implementation
    int width_ = 0;
    int height_ = 0;
    
    gpu::GPUDevice* device_ = nullptr;
    
    // G-buffer textures
    std::unique_ptr<gpu::GPUTexture> gbuffer_position_;
    std::unique_ptr<gpu::GPUTexture> gbuffer_normal_;
    std::unique_ptr<gpu::GPUTexture> gbuffer_depth_;
    
    // SSAO textures
    std::unique_ptr<gpu::GPUTexture> ssao_texture_;
    std::unique_ptr<gpu::GPUTexture> ssao_blur_texture_;
    std::unique_ptr<gpu::GPUTexture> ssao_noise_;
    
    // Samplers
    SDL_GPUSampler* ssao_sampler_ = nullptr;
    
    // SSAO kernel
    std::vector<glm::vec3> ssao_kernel_;
};

} // namespace mmo
