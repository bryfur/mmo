#pragma once

#include "gpu_device.hpp"
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>

namespace mmo::gpu {

/**
 * @brief Shader stage types
 */
enum class ShaderStage {
    Vertex,
    Fragment,
};

/**
 * @brief Shader resource requirements for pipeline creation
 *
 * NOTE: These counts are not validated against hardware limits. The caller is
 * responsible for ensuring values don't exceed device capabilities. Exceeding
 * limits may cause silent failures or crashes on some hardware. Consider
 * querying SDL_GetGPUDeviceProperties() for device limits in production code.
 */
struct ShaderResources {
    uint32_t num_samplers = 0;
    uint32_t num_storage_textures = 0;
    uint32_t num_storage_buffers = 0;
    uint32_t num_uniform_buffers = 0;
};

/**
 * @brief GPU Shader wrapper for pre-compiled SPIRV shaders
 *
 * Shaders are compiled from HLSL to SPIRV at build time by CMake.
 * At runtime, SDL_shadercross converts SPIRV to the backend format
 * (Vulkan uses SPIRV directly, Metal/D3D12 get transpiled).
 *
 * Usage:
 *   auto vs = GPUShader::load_spirv(device, "shaders/model.vert.spv",
 *                                    ShaderStage::Vertex, "VSMain", resources);
 */
class GPUShader {
public:
    ~GPUShader();

    // Non-copyable, movable
    GPUShader(const GPUShader&) = delete;
    GPUShader& operator=(const GPUShader&) = delete;
    GPUShader(GPUShader&& other) noexcept;
    GPUShader& operator=(GPUShader&& other) noexcept;

    /**
     * @brief Load a shader from pre-compiled SPIRV file
     *
     * Uses SDL_shadercross to convert SPIRV to backend format at runtime.
     *
     * @param device The GPU device
     * @param path Path to the compiled .spv file
     * @param stage Shader stage (Vertex or Fragment)
     * @param entry_point Entry point function name (e.g., "VSMain", "PSMain")
     * @param resources Shader resource requirements
     * @return Unique pointer to the shader, or nullptr on failure
     */
    static std::unique_ptr<GPUShader> load_spirv(
        GPUDevice& device,
        const std::string& path,
        ShaderStage stage,
        const std::string& entry_point = "main",
        const ShaderResources& resources = {});

    /**
     * @brief Create a shader from SPIRV bytecode in memory
     */
    static std::unique_ptr<GPUShader> create_from_spirv(
        GPUDevice& device,
        const std::vector<uint8_t>& spirv_bytecode,
        ShaderStage stage,
        const std::string& entry_point = "main",
        const ShaderResources& resources = {});

    // =========================================================================
    // Accessors
    // =========================================================================

    SDL_GPUShader* handle() const { return shader_; }
    ShaderStage stage() const { return stage_; }

    // =========================================================================
    // Global Configuration
    // =========================================================================

    /**
     * @brief Initialize the shader cross-compilation system
     * Must be called before any shader loading.
     */
    static bool init_compiler();

    /**
     * @brief Shutdown the shader cross-compilation system
     */
    static void shutdown_compiler();

    /**
     * @brief Check if the compiler is available
     */
    static bool is_compiler_available();

private:
    GPUShader() = default;

    GPUDevice* device_ = nullptr;
    SDL_GPUShader* shader_ = nullptr;
    ShaderStage stage_ = ShaderStage::Vertex;

    static std::vector<uint8_t> read_file(const std::string& path);

    static bool s_compiler_initialized;
};

/**
 * @brief High-level shader manager with in-memory caching
 *
 * This is the recommended way to load shaders. It provides:
 * - Automatic SPIRV loading with backend conversion
 * - In-memory caching (avoid reloading within session)
 *
 * Usage:
 *   ShaderManager shaders(device);
 *
 *   auto* vs = shaders.get_vertex("shaders/model.vert.spv", "VSMain");
 *   auto* fs = shaders.get_fragment("shaders/model.frag.spv", "PSMain");
 */
class ShaderManager {
public:
    /**
     * @brief Create a shader manager
     * @param device The GPU device
     */
    explicit ShaderManager(GPUDevice& device);
    ~ShaderManager();

    // Non-copyable
    ShaderManager(const ShaderManager&) = delete;
    ShaderManager& operator=(const ShaderManager&) = delete;

    /**
     * @brief Load or get a cached vertex shader
     */
    GPUShader* get_vertex(const std::string& path,
                          const std::string& entry_point = "main",
                          const ShaderResources& resources = {});

    /**
     * @brief Load or get a cached fragment shader
     */
    GPUShader* get_fragment(const std::string& path,
                            const std::string& entry_point = "main",
                            const ShaderResources& resources = {});

    /**
     * @brief Load or get a shader (generic)
     */
    GPUShader* get(const std::string& path,
                   ShaderStage stage,
                   const std::string& entry_point,
                   const ShaderResources& resources = {});

    /**
     * @brief Clear all cached shaders
     */
    void clear_cache();

    /**
     * @brief Reload a shader (clear from cache, reloads on next get)
     * @return true if shader was in cache
     */
    bool reload(const std::string& path);

    /**
     * @brief Reload all shaders
     * @return Number of shaders cleared
     */
    int reload_all();

private:
    GPUDevice& device_;

    // NOTE: This cache is NOT thread-safe. If multi-threaded shader loading
    // is needed, add mutex protection around cache operations, or ensure all
    // shader loading happens on a single thread (e.g., the main/render thread).
    std::unordered_map<std::string, std::unique_ptr<GPUShader>> cache_;

    std::string make_cache_key(const std::string& path, ShaderStage stage,
                               const std::string& entry_point) const;
};

/**
 * @brief Shader program combining vertex and fragment shaders
 */
class ShaderProgram {
public:
    ~ShaderProgram() = default;

    ShaderProgram(const ShaderProgram&) = delete;
    ShaderProgram& operator=(const ShaderProgram&) = delete;
    ShaderProgram(ShaderProgram&&) = default;
    ShaderProgram& operator=(ShaderProgram&&) = default;

    /**
     * @brief Load shader program from pre-compiled SPIRV files
     */
    static std::unique_ptr<ShaderProgram> load(
        GPUDevice& device,
        const std::string& vertex_path,
        const std::string& fragment_path,
        const std::string& vertex_entry = "VSMain",
        const std::string& fragment_entry = "PSMain");

    SDL_GPUShader* vertex_shader() const { return vertex_ ? vertex_->handle() : nullptr; }
    SDL_GPUShader* fragment_shader() const { return fragment_ ? fragment_->handle() : nullptr; }

private:
    ShaderProgram() = default;

    std::unique_ptr<GPUShader> vertex_;
    std::unique_ptr<GPUShader> fragment_;
};

} // namespace mmo::gpu
