#pragma once

#include "gpu_device.hpp"
#include "gpu_pipeline.hpp"
#include "gpu_shader.hpp"
#include <memory>
#include <string>
#include <unordered_map>


namespace mmo::engine::gpu {

class ShaderManager;

/**
 * @brief Pipeline type enumeration for all supported rendering pipelines
 */
enum class PipelineType {
    // 3D rendering pipelines
    Model,              ///< Static 3D models
    SkinnedModel,       ///< Animated/skeletal models
    Terrain,            ///< Terrain rendering with splatmap
    Skybox,             ///< Skybox cubemap rendering
    Grid,               ///< Debug grid overlay
    
    // 2D/UI pipelines
    UI,                 ///< 2D UI elements
    Text,               ///< Text rendering with font atlas
    Billboard,          ///< Camera-facing billboards
    
    // Effect pipelines
    Effect,             ///< Particle effects (additive blending)
    Grass,              ///< Instanced grass rendering

    // Shadow pipelines (depth-only)
    ShadowModel,        ///< Shadow depth pass for static models
    ShadowSkinnedModel, ///< Shadow depth pass for skinned models
    ShadowTerrain,      ///< Shadow depth pass for terrain

    // Count for iteration
    Count
};

/**
 * @brief Convert pipeline type to string for debugging
 */
inline const char* pipeline_type_to_string(PipelineType type) {
    switch (type) {
        case PipelineType::Model:         return "Model";
        case PipelineType::SkinnedModel:  return "SkinnedModel";
        case PipelineType::Terrain:       return "Terrain";
        case PipelineType::Skybox:        return "Skybox";
        case PipelineType::Grid:          return "Grid";
        case PipelineType::UI:            return "UI";
        case PipelineType::Text:          return "Text";
        case PipelineType::Billboard:     return "Billboard";
        case PipelineType::Effect:        return "Effect";
        case PipelineType::Grass:              return "Grass";
        case PipelineType::ShadowModel:        return "ShadowModel";
        case PipelineType::ShadowSkinnedModel: return "ShadowSkinnedModel";
        case PipelineType::ShadowTerrain:      return "ShadowTerrain";
        default:                               return "Unknown";
    }
}

/**
 * @brief Registry for managing and caching GPU pipelines
 * 
 * The PipelineRegistry provides a central location for creating and caching
 * all graphics pipelines used by the renderer. It handles:
 * - Lazy pipeline creation on first access
 * - Pipeline caching to avoid recreation
 * - Shader compilation and management
 * - Proper resource cleanup on shutdown
 * 
 * Usage:
 *   PipelineRegistry registry;
 *   registry.init(device);
 *   
 *   // Get pipelines (created on demand)
 *   auto* model_pipeline = registry.get_pipeline(PipelineType::Model);
 *   auto* ui_pipeline = registry.get_pipeline(PipelineType::UI);
 *   
 *   // In render loop:
 *   model_pipeline->bind(render_pass);
 *   
 *   registry.shutdown();
 */
class PipelineRegistry {
public:
    PipelineRegistry() = default;
    ~PipelineRegistry();

    // Non-copyable, non-movable (owns GPU resources)
    PipelineRegistry(const PipelineRegistry&) = delete;
    PipelineRegistry& operator=(const PipelineRegistry&) = delete;
    PipelineRegistry(PipelineRegistry&&) = delete;
    PipelineRegistry& operator=(PipelineRegistry&&) = delete;

    /**
     * @brief Initialize the pipeline registry
     * 
     * Must be called before any pipelines can be retrieved.
     * 
     * @param device The GPU device to create pipelines on
     * @return true on success, false on failure
     */
    bool init(GPUDevice& device);

    /**
     * @brief Shutdown and release all cached pipelines
     */
    void shutdown();

    /**
     * @brief Check if the registry is initialized
     */
    bool is_initialized() const { return device_ != nullptr; }

    /**
     * @brief Get a pipeline by type
     * 
     * Pipelines are created lazily on first access. Returns nullptr if
     * the pipeline could not be created.
     * 
     * @param type The pipeline type to retrieve
     * @return Pointer to the pipeline, or nullptr on failure
     */
    GPUPipeline* get_pipeline(PipelineType type);

    // Convenience accessors for common pipeline types
    GPUPipeline* get_model_pipeline() { return get_pipeline(PipelineType::Model); }
    GPUPipeline* get_skinned_model_pipeline() { return get_pipeline(PipelineType::SkinnedModel); }
    GPUPipeline* get_terrain_pipeline() { return get_pipeline(PipelineType::Terrain); }
    GPUPipeline* get_skybox_pipeline() { return get_pipeline(PipelineType::Skybox); }
    GPUPipeline* get_ui_pipeline() { return get_pipeline(PipelineType::UI); }
    GPUPipeline* get_billboard_pipeline() { return get_pipeline(PipelineType::Billboard); }
    GPUPipeline* get_grass_pipeline() { return get_pipeline(PipelineType::Grass); }
    GPUPipeline* get_effect_pipeline() { return get_pipeline(PipelineType::Effect); }
    GPUPipeline* get_text_pipeline() { return get_pipeline(PipelineType::Text); }
    GPUPipeline* get_grid_pipeline() { return get_pipeline(PipelineType::Grid); }
    GPUPipeline* get_shadow_model_pipeline() { return get_pipeline(PipelineType::ShadowModel); }
    GPUPipeline* get_shadow_skinned_model_pipeline() { return get_pipeline(PipelineType::ShadowSkinnedModel); }
    GPUPipeline* get_shadow_terrain_pipeline() { return get_pipeline(PipelineType::ShadowTerrain); }

    /**
     * @brief Pre-create all pipelines
     * 
     * Call this during loading to create all pipelines upfront and avoid
     * hitching during gameplay. Returns false if any pipeline fails to create.
     * 
     * @return true if all pipelines created successfully
     */
    bool preload_all_pipelines();

    /**
     * @brief Get the number of cached pipelines
     */
    size_t cached_pipeline_count() const { return pipelines_.size(); }

    /**
     * @brief Invalidate all cached pipelines
     * 
     * Call this if the swapchain format changes (e.g., window resize that
     * changes color format). Pipelines will be recreated on next access.
     */
    void invalidate_all();

    /**
     * @brief Set the swapchain color format
     * 
     * Must be called before pipelines are created. If called after pipelines
     * exist, they will be invalidated.
     */
    void set_swapchain_format(SDL_GPUTextureFormat format);

    /**
     * @brief Get the current swapchain format
     */
    SDL_GPUTextureFormat get_swapchain_format() const { return swapchain_format_; }

private:
    // Pipeline creation methods
    std::unique_ptr<GPUPipeline> create_model_pipeline();
    std::unique_ptr<GPUPipeline> create_skinned_model_pipeline();
    std::unique_ptr<GPUPipeline> create_terrain_pipeline();
    std::unique_ptr<GPUPipeline> create_skybox_pipeline();
    std::unique_ptr<GPUPipeline> create_grid_pipeline();
    std::unique_ptr<GPUPipeline> create_ui_pipeline();
    std::unique_ptr<GPUPipeline> create_text_pipeline();
    std::unique_ptr<GPUPipeline> create_billboard_pipeline();
    std::unique_ptr<GPUPipeline> create_effect_pipeline();
    std::unique_ptr<GPUPipeline> create_grass_pipeline();
    std::unique_ptr<GPUPipeline> create_shadow_model_pipeline();
    std::unique_ptr<GPUPipeline> create_shadow_skinned_model_pipeline();
    std::unique_ptr<GPUPipeline> create_shadow_terrain_pipeline();

    GPUDevice* device_ = nullptr;
    SDL_GPUTextureFormat swapchain_format_ = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    SDL_GPUTextureFormat depth_format_ = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;

    // Pipeline cache
    std::unordered_map<PipelineType, std::unique_ptr<GPUPipeline>> pipelines_;

    // Shader manager for loading shaders from files
    std::unique_ptr<ShaderManager> shader_manager_;

    // Base path for compiled SPIRV shader files (relative to working directory)
    std::string shader_path_ = "shaders/";
};

} // namespace mmo::engine::gpu
