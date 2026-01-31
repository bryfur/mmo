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
static constexpr int CSM_CASCADE_COUNT = 4;
static constexpr int SHADOW_MAP_RESOLUTION = 2048;

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

    bool init(gpu::GPUDevice& device);
    void shutdown();

    void update(const glm::mat4& camera_view, const glm::mat4& camera_proj,
                const glm::vec3& light_dir, float near_plane, float far_plane);

    SDL_GPURenderPass* begin_shadow_pass(SDL_GPUCommandBuffer* cmd, int cascade_index);
    void end_shadow_pass();

    // Accessors
    const std::array<CascadeData, CSM_CASCADE_COUNT>& cascades() const { return cascades_; }

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
    std::array<std::unique_ptr<gpu::GPUTexture>, CSM_CASCADE_COUNT> cascade_textures_;
    SDL_GPUSampler* shadow_sampler_ = nullptr;
    SDL_GPURenderPass* current_shadow_pass_ = nullptr;

    std::array<CascadeData, CSM_CASCADE_COUNT> cascades_ = {};
};

} // namespace mmo::engine::render
