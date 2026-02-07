#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cstdint>
#include <algorithm>
#include <memory>

// Include GPU abstraction headers for proper type definitions
#include "gpu/gpu_buffer.hpp"
#include "gpu/gpu_texture.hpp"

// Animation types from the dedicated animation module
#include "animation/animation_types.hpp"

// Forward declarations for SDL3 GPU types
struct SDL_GPUDevice;
struct SDL_GPUCommandBuffer;

namespace mmo::engine {

// Re-export animation types into mmo::engine for backward compatibility
using animation::MAX_BONES;
using animation::MAX_BONE_INFLUENCES;
using animation::AnimationKeyframe;
using animation::AnimationChannel;
using animation::AnimationClip;
using animation::Joint;
using animation::Skeleton;
using animation::FootIKData;

struct Vertex3D {
    float x, y, z;       // Position
    float nx, ny, nz;    // Normal
    float u, v;          // Texture coords
    float r, g, b, a;    // Vertex color
};

// Skinned vertex with bone weights
struct SkinnedVertex {
    float x, y, z;       // Position
    float nx, ny, nz;    // Normal
    float u, v;          // Texture coords
    float r, g, b, a;    // Vertex color
    uint8_t joints[4];   // Bone indices (up to 4 influences)
    float weights[4];    // Bone weights (sum to 1.0)
};

struct Mesh {
    std::vector<Vertex3D> vertices;
    std::vector<SkinnedVertex> skinned_vertices;  // Used if model has skeleton
    std::vector<uint32_t> indices;

    // SDL3 GPU resources
    std::unique_ptr<gpu::GPUBuffer> vertex_buffer;
    std::unique_ptr<gpu::GPUBuffer> index_buffer;
    std::unique_ptr<gpu::GPUTexture> texture;
    bool uploaded = false;

    bool has_texture = false;
    uint32_t base_color = 0xFFFFFFFF;
    bool is_skinned = false;  // True if using skinned vertices

    // Texture data for deferred upload (before GPU upload)
    std::vector<uint8_t> texture_pixels;
    int texture_width = 0;
    int texture_height = 0;

    // Convenience accessors
    uint32_t vertex_count() const {
        return is_skinned ? static_cast<uint32_t>(skinned_vertices.size())
                          : static_cast<uint32_t>(vertices.size());
    }
    uint32_t index_count() const { return static_cast<uint32_t>(indices.size()); }

    // Bind vertex and index buffers for rendering (SDL3 GPU API)
    void bind_buffers(struct SDL_GPURenderPass* pass) const;
};

struct Model {
    std::vector<Mesh> meshes;
    float min_x = 0, min_y = 0, min_z = 0;
    float max_x = 0, max_y = 0, max_z = 0;
    bool loaded = false;

    // Skeletal animation data
    Skeleton skeleton;
    std::vector<AnimationClip> animations;
    bool has_skeleton = false;
    FootIKData foot_ik;

    float width() const { return max_x - min_x; }
    float height() const { return max_y - min_y; }
    float depth() const { return max_z - min_z; }
    float max_dimension() const {
        return std::max({width(), height(), depth()});
    }

    // Animation helpers
    int find_animation(const std::string& name) const {
        for (size_t i = 0; i < animations.size(); i++) {
            if (animations[i].name == name) return static_cast<int>(i);
        }
        return -1;
    }
};

class ModelLoader {
public:
    static bool load_glb(const std::string& path, Model& model);
    static void upload_to_gpu(gpu::GPUDevice& device, Model& model);
    static void free_gpu_resources(Model& model);
};

class ModelManager {
public:
    ModelManager() = default;
    ~ModelManager();

    void set_device(gpu::GPUDevice* device) { device_ = device; }

    bool load_model(const std::string& name, const std::string& path);
    Model* get_model(const std::string& name);
    void unload_all();

private:
    gpu::GPUDevice* device_ = nullptr;
    std::unordered_map<std::string, Model> models_;
};

} // namespace mmo::engine
