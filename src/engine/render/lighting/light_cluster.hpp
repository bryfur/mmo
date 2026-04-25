#pragma once

#include "engine/gpu/gpu_buffer.hpp"
#include "engine/gpu/gpu_device.hpp"
#include "engine/memory/arena.hpp"
#include "engine/render/lighting/light.hpp"
#include "engine/scene/camera_state.hpp"
#include <array>
#include <cstdint>
#include <glm/glm.hpp>
#include <memory>
#include <SDL3/SDL_gpu.h>
#include <vector>

namespace mmo::engine::render::lighting {

// Std140-compatible layout shared with clusters.hlsli.
// All fields packed contiguously; HLSL cbuffer layout requires 16-byte alignment.
struct alignas(16) ClusterParams {
    glm::mat4 view;
    glm::mat4 invProjection;
    glm::vec4 screenSize;     // (width, height, 1/width, 1/height)
    glm::vec4 zPlanes;        // (near, far, log(far/near), 1/log(far/near))
    glm::uvec4 gridDim;       // (X, Y, Z, totalLightCount)
    glm::uvec4 maxPerCluster; // (max_lights_per_cluster, _, _, _)
};

class ClusterGrid {
public:
    ClusterGrid() = default;
    ~ClusterGrid();

    ClusterGrid(const ClusterGrid&) = delete;
    ClusterGrid& operator=(const ClusterGrid&) = delete;

    bool init(gpu::GPUDevice& device, uint32_t max_lights = MAX_LIGHTS);
    void shutdown();

    // Reset per-frame light lists and capture camera state.
    void begin_frame(const scene::CameraState& camera, uint32_t screen_w, uint32_t screen_h, float near_plane,
                     float far_plane);

    void add_point_light(const PointLight& l);
    void add_spot_light(const SpotLight& l);

    // CPU-side bin lights into clusters; populates the per-cluster offset/count
    // table and concatenated light index list.
    void build();

    // Upload light data + cluster offsets + light indices SSBOs.
    // Pass a frame arena to avoid heap-allocating the staging buffer each frame.
    void upload(SDL_GPUCommandBuffer* cb, memory::Arena* frame_arena = nullptr);

    SDL_GPUBuffer* light_data_buffer() const;
    SDL_GPUBuffer* cluster_offsets_buffer() const;
    SDL_GPUBuffer* light_indices_buffer() const;

    const ClusterParams& params() const { return params_; }

    // Test/debug accessors.
    const std::vector<PointLight>& point_lights() const { return points_; }
    const std::vector<SpotLight>& spot_lights() const { return spots_; }
    const std::vector<uint32_t>& light_indices() const { return light_indices_; }
    // Per-cluster (offset, count) packed as uvec2 — flat array of 2*CLUSTER_COUNT u32.
    const std::vector<uint32_t>& cluster_offsets() const { return cluster_offsets_; }
    uint32_t total_lights() const { return static_cast<uint32_t>(points_.size() + spots_.size()); }

private:
    // Linear cluster -> sphere/AABB params, computed in begin_frame() from camera.
    struct ClusterBounds {
        glm::vec3 view_min;
        glm::vec3 view_max;
    };

    void compute_cluster_bounds();
    static bool sphere_intersects_aabb(const glm::vec3& center, float radius, const glm::vec3& aabb_min,
                                       const glm::vec3& aabb_max);

    gpu::GPUDevice* device_ = nullptr;
    uint32_t max_lights_ = MAX_LIGHTS;

    // Per-frame CPU state.
    std::vector<PointLight> points_;
    std::vector<SpotLight> spots_;
    std::vector<LightHeader> headers_;          // size = points + spots
    std::vector<ClusterBounds> cluster_bounds_; // size = CLUSTER_COUNT
    std::vector<uint32_t> cluster_offsets_;     // 2 * CLUSTER_COUNT (offset, count) flat
    std::vector<uint32_t> light_indices_;       // packed indices into headers_
    std::vector<std::vector<uint32_t>> bins_;   // scratch, CLUSTER_COUNT entries

    ClusterParams params_{};
    glm::mat4 last_inv_projection_{0.0f};
    float last_near_ = 0.0f;
    float last_far_ = 0.0f;
    bool bounds_valid_ = false;
    bool warned_truncation_ = false;

    // GPU buffers (storage buffers, dynamic).
    std::unique_ptr<gpu::GPUBuffer> light_data_buf_;
    std::unique_ptr<gpu::GPUBuffer> cluster_offsets_buf_;
    std::unique_ptr<gpu::GPUBuffer> light_indices_buf_;
};

} // namespace mmo::engine::render::lighting
