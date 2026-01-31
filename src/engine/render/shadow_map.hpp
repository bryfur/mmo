#pragma once

#include "engine/gpu/gpu_device.hpp"
#include "engine/gpu/gpu_texture.hpp"
#include "engine/gpu/gpu_uniforms.hpp"
#include "engine/gpu/pipeline_registry.hpp"
#include <glm/glm.hpp>
#include <array>
#include <memory>

namespace mmo::engine::render {

namespace gpu = mmo::engine::gpu;

/// Shadow map configuration
static constexpr int CSM_MAX_CASCADES = 4;

/// Per-cascade data
struct CascadeData {
    glm::mat4 light_view_projection;
    float split_depth;  // view-space far distance for this cascade
};

/**
 * Cascaded Shadow Map manager.
 *
 * Owns 4 individual depth textures (one per cascade) for both rendering
 * and sampling. SDL3 GPU doesn't support depth array render targets, so
 * each cascade gets its own Texture2D.
 *
 * Fragment shaders select the cascade via if/else and sample from
 * the appropriate texture.
 */
class ShadowMap {
public:
    ShadowMap();
    ~ShadowMap();

    ShadowMap(const ShadowMap&) = delete;
    ShadowMap& operator=(const ShadowMap&) = delete;

    bool init(gpu::GPUDevice& device, int resolution = 2048);
    void shutdown();

    /// Reinitialize with a new resolution (destroys and recreates textures)
    bool reinit(int resolution);

    /// Set number of active cascades (1-4), does not require reinit
    void set_active_cascades(int count);
    int active_cascades() const { return active_cascades_; }
    int resolution() const { return resolution_; }

    void update(const glm::mat4& camera_view, const glm::mat4& camera_proj,
                const glm::vec3& light_dir, float near_plane, float far_plane);

    SDL_GPURenderPass* begin_shadow_pass(SDL_GPUCommandBuffer* cmd, int cascade_index);
    void end_shadow_pass();

    // Accessors
    const std::array<CascadeData, CSM_MAX_CASCADES>& cascades() const { return cascades_; }

    /// Returns the shadow texture handle for a specific cascade
    SDL_GPUTexture* shadow_texture(int cascade) const;

    /// Returns true if all shadow textures are ready
    bool is_ready() const;

    SDL_GPUSampler* shadow_sampler() const { return shadow_sampler_; }

    gpu::ShadowDataUniforms get_shadow_uniforms(int shadow_mode = 2) const;

    float light_size = 8.0f;
    float split_lambda = 0.5f;

private:
    void compute_cascade_splits(float near_plane, float far_plane);
    glm::mat4 compute_cascade_matrix(const glm::mat4& camera_view, const glm::mat4& camera_proj,
                                     const glm::vec3& light_dir,
                                     float near_split, float far_split);

    gpu::GPUDevice* device_ = nullptr;
    std::array<std::unique_ptr<gpu::GPUTexture>, CSM_MAX_CASCADES> cascade_textures_;
    SDL_GPUSampler* shadow_sampler_ = nullptr;
    SDL_GPURenderPass* current_shadow_pass_ = nullptr;

    int active_cascades_ = CSM_MAX_CASCADES;
    int resolution_ = 2048;
    std::array<CascadeData, CSM_MAX_CASCADES> cascades_ = {};
};

} // namespace mmo::engine::render
