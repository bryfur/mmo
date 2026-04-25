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
#include "gpu/gpu_types.hpp"

// Animation types from the dedicated animation module
#include "animation/animation_types.hpp"

// Forward declarations for SDL3 GPU types
struct SDL_GPUDevice;
struct SDL_GPUCommandBuffer;

namespace mmo::engine::core::asset { class FileWatcher; }

namespace mmo::engine {

// Integer handle for O(1) model lookup (replaces string-based lookups in hot paths)
using ModelHandle = uint32_t;
static constexpr ModelHandle INVALID_MODEL_HANDLE = 0;

// Use canonical vertex types from gpu module
using gpu::Vertex3D;
using gpu::SkinnedVertex;

// PBR material parameters mirroring glTF pbrMetallicRoughness.
// Textures are uploaded on demand; scalar factors act as fallbacks.
struct PBRMaterial {
    glm::vec4 base_color_factor{1.0f, 1.0f, 1.0f, 1.0f};
    float metallic_factor = 0.0f;
    float roughness_factor = 0.85f;
    float normal_scale = 1.0f;
    float occlusion_strength = 1.0f;
    // CPU-side raw pixels for deferred GPU upload. Integration into fragment
    // shaders is planned; currently only base_color is sampled.
    std::vector<uint8_t> metallic_roughness_pixels;
    int metallic_roughness_width = 0;
    int metallic_roughness_height = 0;
    std::vector<uint8_t> normal_pixels;
    int normal_width = 0;
    int normal_height = 0;
    std::vector<uint8_t> occlusion_pixels;
    int occlusion_width = 0;
    int occlusion_height = 0;
    std::unique_ptr<gpu::GPUTexture> metallic_roughness_texture;
    std::unique_ptr<gpu::GPUTexture> normal_texture;
    std::unique_ptr<gpu::GPUTexture> occlusion_texture;
};

struct Mesh {
    std::vector<Vertex3D> vertices;
    std::vector<SkinnedVertex> skinned_vertices;  // Used if model has skeleton
    std::vector<uint32_t> indices;

    // SDL3 GPU resources
    std::unique_ptr<gpu::GPUBuffer> vertex_buffer;
    std::unique_ptr<gpu::GPUBuffer> index_buffer;
    std::unique_ptr<gpu::GPUTexture> texture;
    PBRMaterial material;
    bool uploaded = false;

    bool has_texture = false;
    uint32_t base_color = 0xFFFFFFFF;
    bool is_skinned = false;  // True if using skinned vertices

    // Perf flags.
    //
    // cast_shadows=false : shadow passes skip this mesh (e.g. transparent leaves
    //   that would cause expensive alpha-test overdraw in shadow depth rasterization).
    // shadow_only=true   : main pass skips this mesh; only shadow passes render it.
    //   Used for canopy shadow proxies — a low-poly ellipsoid that approximates the
    //   leaf silhouette for shadows, replacing thousands of individual leaf quads.
    // near_only=true     : hint that this mesh should only render near the camera
    //   (high-frequency detail). Not yet enforced.
    bool cast_shadows = true;
    bool shadow_only = false;
    bool near_only = false;

    // Texture data for deferred upload (before GPU upload)
    std::vector<uint8_t> texture_pixels;
    int texture_width = 0;
    int texture_height = 0;

    // Cached index count (valid after GPU upload, when CPU-side data is freed)
    uint32_t cached_index_count = 0;

    // Convenience accessors
    uint32_t vertex_count() const {
        return is_skinned ? static_cast<uint32_t>(skinned_vertices.size())
                          : static_cast<uint32_t>(vertices.size());
    }
    uint32_t index_count() const {
        return cached_index_count > 0 ? cached_index_count : static_cast<uint32_t>(indices.size());
    }

    // Bind vertex and index buffers for rendering (SDL3 GPU API)
    void bind_buffers(struct SDL_GPURenderPass* pass) const;
};

struct Model {
    std::vector<Mesh> meshes;
    float min_x = 0, min_y = 0, min_z = 0;
    float max_x = 0, max_y = 0, max_z = 0;
    bool loaded = false;

    // Skeletal animation data
    animation::Skeleton skeleton;
    std::vector<animation::AnimationClip> animations;
    bool has_skeleton = false;
    animation::FootIKData foot_ik;

    // Pre-computed bounding sphere for fast frustum culling (avoids per-entity recomputation)
    glm::vec3 bounding_center = {0, 0, 0};
    float bounding_half_diag = 0.0f;

    float width() const { return max_x - min_x; }
    float height() const { return max_y - min_y; }
    float depth() const { return max_z - min_z; }
    float max_dimension() const {
        return std::max({width(), height(), depth()});
    }

    void compute_bounding_sphere() {
        bounding_center = glm::vec3(
            (min_x + max_x) * 0.5f,
            (min_y + max_y) * 0.5f,
            (min_z + max_z) * 0.5f
        );
        bounding_half_diag = glm::length(glm::vec3(width(), height(), depth())) * 0.5f;
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
    ModelManager();
    ~ModelManager();

    void set_device(gpu::GPUDevice* device) { device_ = device; }

    /// Load a model and return its handle (INVALID_MODEL_HANDLE on failure)
    ModelHandle load_model(const std::string& name, const std::string& path);

    /// O(1) lookup by handle
    Model* get_model(ModelHandle handle) const;

    /// Name-based lookup (backward compat, uses hash map)
    Model* get_model(const std::string& name) const;

    /// Resolve a name to a handle (INVALID_MODEL_HANDLE if not found)
    ModelHandle get_handle(const std::string& name) const;

    /// Register a pre-built model (e.g. procedurally generated). Takes ownership.
    /// Uploads to GPU if device is set. Returns handle.
    ModelHandle register_model(const std::string& name, std::unique_ptr<Model> model);

    /// Re-parse the source file and swap GPU buffers in place; the existing
    /// ModelHandle keeps pointing at the same slot. Returns false if the file
    /// failed to load (the existing model is left untouched).
    bool reload_model(ModelHandle handle);

    /// Enable hot-reload: re-load any model whose source file (.glb/.gltf) is
    /// modified on disk.
    void enable_hot_reload(core::asset::FileWatcher& watcher);

    void unload_all();

private:
    gpu::GPUDevice* device_ = nullptr;
    // Slot 0 is reserved (INVALID_MODEL_HANDLE), models start at index 1
    std::vector<std::unique_ptr<Model>> models_;
    std::vector<std::string> model_paths_;  // parallel to models_; "" for procedural
    std::unordered_map<std::string, ModelHandle> name_to_handle_;
    core::asset::FileWatcher* watcher_ = nullptr;
};

} // namespace mmo::engine
