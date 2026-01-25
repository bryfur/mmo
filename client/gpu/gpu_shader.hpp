#pragma once

#include "gpu_device.hpp"
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <filesystem>
#include <functional>

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
 */
struct ShaderResources {
    uint32_t num_samplers = 0;
    uint32_t num_storage_textures = 0;
    uint32_t num_storage_buffers = 0;
    uint32_t num_uniform_buffers = 0;
};

/**
 * @brief GPU Shader wrapper with runtime HLSL compilation and caching
 * 
 * This class handles both pre-compiled shader bytecode and runtime HLSL compilation
 * using SDL_shadercross. It supports disk-based caching to avoid recompilation.
 * 
 * Compilation Flow:
 *   1. Check in-memory cache
 *   2. Check disk cache (if enabled)
 *   3. Compile from HLSL source (if cache miss)
 *   4. Save to disk cache (if enabled)
 * 
 * Usage (Runtime compilation - recommended for development):
 *   auto vs = GPUShader::compile_from_hlsl(device, hlsl_source, ShaderStage::Vertex, "VSMain");
 *   
 * Usage (Load HLSL file with auto-compilation):
 *   auto vs = GPUShader::load_hlsl(device, "shaders/src/model.vert.hlsl", ShaderStage::Vertex, "VSMain");
 *   
 * Usage (Pre-compiled bytecode):
 *   auto vs = GPUShader::load_from_file(device, "shaders/compiled/model.vert.spv", ShaderStage::Vertex);
 */
class GPUShader {
public:
    ~GPUShader();

    // Non-copyable, movable
    GPUShader(const GPUShader&) = delete;
    GPUShader& operator=(const GPUShader&) = delete;
    GPUShader(GPUShader&& other) noexcept;
    GPUShader& operator=(GPUShader&& other) noexcept;

    // =========================================================================
    // Runtime HLSL Compilation (Recommended for Development)
    // =========================================================================

    /**
     * @brief Compile a shader from HLSL source string
     * 
     * Uses SDL_shadercross to compile HLSL to the appropriate backend format
     * (SPIR-V, Metal, DXIL) based on the current GPU device.
     * 
     * @param device The GPU device
     * @param hlsl_source HLSL shader source code
     * @param stage Shader stage (Vertex or Fragment)
     * @param entry_point Entry point function name (e.g., "VSMain", "PSMain")
     * @param resources Shader resource requirements
     * @return Unique pointer to the shader, or nullptr on failure
     */
    static std::unique_ptr<GPUShader> compile_from_hlsl(
        GPUDevice& device,
        const std::string& hlsl_source,
        ShaderStage stage,
        const std::string& entry_point = "main",
        const ShaderResources& resources = {});

    /**
     * @brief Load and compile an HLSL shader from file
     * 
     * Loads the HLSL source from file and compiles it. Uses disk cache if enabled.
     * 
     * @param device The GPU device
     * @param path Path to the HLSL source file
     * @param stage Shader stage
     * @param entry_point Entry point function name
     * @param resources Shader resource requirements
     * @return Unique pointer to the shader, or nullptr on failure
     */
    static std::unique_ptr<GPUShader> load_hlsl(
        GPUDevice& device,
        const std::string& path,
        ShaderStage stage,
        const std::string& entry_point = "main",
        const ShaderResources& resources = {});

    // =========================================================================
    // Pre-compiled Bytecode Loading
    // =========================================================================

    /**
     * @brief Load a shader from pre-compiled bytecode file
     * 
     * @param device The GPU device
     * @param path Path to the compiled shader file (.spv, .metallib, .dxil)
     * @param stage Shader stage
     * @param entry_point Entry point function name
     * @param resources Shader resource requirements
     * @return Unique pointer to the shader, or nullptr on failure
     */
    static std::unique_ptr<GPUShader> load_from_file(
        GPUDevice& device,
        const std::string& path,
        ShaderStage stage,
        const std::string& entry_point = "main",
        const ShaderResources& resources = {});

    /**
     * @brief Create a shader from bytecode in memory
     */
    static std::unique_ptr<GPUShader> create_from_bytecode(
        GPUDevice& device,
        const std::vector<uint8_t>& bytecode,
        ShaderStage stage,
        SDL_GPUShaderFormat format,
        const std::string& entry_point = "main",
        const ShaderResources& resources = {});

    /**
     * @brief Create a shader from SPIRV bytecode in memory
     * 
     * This is used by the shader cache to skip the HLSL->SPIRV compilation step.
     */
    static std::unique_ptr<GPUShader> create_from_spirv(
        GPUDevice& device,
        const std::vector<uint8_t>& spirv_bytecode,
        ShaderStage stage,
        const std::string& entry_point = "main",
        const ShaderResources& resources = {});

    /**
     * @brief Compile from HLSL and also output the intermediate SPIRV bytecode
     * 
     * This allows caching the SPIRV for faster subsequent loads.
     * 
     * @param spirv_out If non-null, receives the compiled SPIRV bytecode
     */
    static std::unique_ptr<GPUShader> compile_from_hlsl_with_spirv(
        GPUDevice& device,
        const std::string& hlsl_source,
        ShaderStage stage,
        const std::string& entry_point,
        const ShaderResources& resources,
        std::vector<uint8_t>* spirv_out);

    // =========================================================================
    // Accessors
    // =========================================================================

    SDL_GPUShader* handle() const { return shader_; }
    ShaderStage stage() const { return stage_; }

    // =========================================================================
    // Global Configuration
    // =========================================================================

    /**
     * @brief Initialize the shader compilation system
     * Must be called before any shader compilation.
     */
    static bool init_compiler();

    /**
     * @brief Shutdown the shader compilation system
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

    static SDL_GPUShaderFormat detect_format_from_path(const std::string& path);
    static std::vector<uint8_t> read_file(const std::string& path);
    static std::string read_text_file(const std::string& path);
    
    static bool s_compiler_initialized;
};

/**
 * @brief Disk-based shader cache for compiled bytecode
 * 
 * Caches compiled shader bytecode to disk to avoid recompilation on subsequent runs.
 * Uses a hash of the source code + entry point + stage to identify cached shaders.
 * 
 * Cache structure:
 *   cache_dir/
 *     {hash}.spv      - SPIR-V bytecode
 *     {hash}.metal    - Metal bytecode  
 *     {hash}.dxil     - DXIL bytecode
 *     {hash}.meta     - Metadata (source hash, timestamp, etc.)
 */
class ShaderDiskCache {
public:
    /**
     * @brief Create a shader disk cache
     * @param cache_dir Directory to store cached shaders
     */
    explicit ShaderDiskCache(const std::filesystem::path& cache_dir);
    ~ShaderDiskCache() = default;

    /**
     * @brief Enable or disable the cache
     */
    void set_enabled(bool enabled) { enabled_ = enabled; }
    bool is_enabled() const { return enabled_; }

    /**
     * @brief Clear all cached shaders
     */
    void clear();

    /**
     * @brief Get cached bytecode if available and valid
     * 
     * @param source_hash Hash of the shader source
     * @param format Target shader format
     * @return Cached bytecode, or empty vector if not found/invalid
     */
    std::vector<uint8_t> get(const std::string& source_hash, SDL_GPUShaderFormat format);

    /**
     * @brief Store compiled bytecode in cache
     * 
     * @param source_hash Hash of the shader source
     * @param format Target shader format
     * @param bytecode Compiled bytecode to cache
     */
    void put(const std::string& source_hash, SDL_GPUShaderFormat format,
             const std::vector<uint8_t>& bytecode);

    /**
     * @brief Compute a hash for shader source code
     */
    static std::string compute_hash(const std::string& source, 
                                     ShaderStage stage,
                                     const std::string& entry_point);

private:
    std::filesystem::path cache_dir_;
    bool enabled_ = true;

    std::filesystem::path get_cache_path(const std::string& hash, SDL_GPUShaderFormat format) const;
    static std::string format_extension(SDL_GPUShaderFormat format);
};

/**
 * @brief High-level shader manager with in-memory and disk caching
 * 
 * This is the recommended way to load shaders. It provides:
 * - Automatic HLSL compilation
 * - In-memory caching (avoid recompilation within session)
 * - Disk caching (avoid recompilation across sessions)
 * - Hot-reload support (planned)
 * 
 * Usage:
 *   ShaderManager shaders(device, "shaders/cache");
 *   
 *   auto* vs = shaders.get_vertex("shaders/src/model.vert.hlsl", "VSMain");
 *   auto* fs = shaders.get_fragment("shaders/src/model.frag.hlsl", "PSMain");
 */
class ShaderManager {
public:
    /**
     * @brief Create a shader manager
     * @param device The GPU device
     * @param cache_dir Directory for disk cache (empty to disable disk caching)
     */
    ShaderManager(GPUDevice& device, const std::string& cache_dir = "");
    ~ShaderManager();

    // Non-copyable
    ShaderManager(const ShaderManager&) = delete;
    ShaderManager& operator=(const ShaderManager&) = delete;

    /**
     * @brief Load or get a cached vertex shader from HLSL file
     */
    GPUShader* get_vertex(const std::string& path, 
                          const std::string& entry_point = "VSMain",
                          const ShaderResources& resources = {});

    /**
     * @brief Load or get a cached fragment shader from HLSL file
     */
    GPUShader* get_fragment(const std::string& path,
                            const std::string& entry_point = "PSMain", 
                            const ShaderResources& resources = {});

    /**
     * @brief Load or get a shader (generic)
     */
    GPUShader* get(const std::string& path, 
                   ShaderStage stage,
                   const std::string& entry_point,
                   const ShaderResources& resources = {});

    /**
     * @brief Clear all in-memory cached shaders
     */
    void clear_memory_cache();

    /**
     * @brief Clear the disk cache
     */
    void clear_disk_cache();

    /**
     * @brief Enable or disable disk caching
     */
    void set_disk_cache_enabled(bool enabled);

    /**
     * @brief Reload a shader (recompile from source)
     * @return true if reload succeeded
     */
    bool reload(const std::string& path);

    /**
     * @brief Reload all shaders
     * @return Number of shaders successfully reloaded
     */
    int reload_all();

private:
    GPUDevice& device_;
    std::unique_ptr<ShaderDiskCache> disk_cache_;
    std::unordered_map<std::string, std::unique_ptr<GPUShader>> memory_cache_;
    std::unordered_map<std::string, std::string> path_to_source_; // For reload

    std::string make_cache_key(const std::string& path, ShaderStage stage,
                               const std::string& entry_point) const;

    GPUShader* load_with_cache(const std::string& path, ShaderStage stage,
                               const std::string& entry_point,
                               const ShaderResources& resources);
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
     * @brief Load shader program from HLSL files
     */
    static std::unique_ptr<ShaderProgram> load_hlsl(
        GPUDevice& device,
        const std::string& vertex_path,
        const std::string& fragment_path,
        const std::string& vertex_entry = "VSMain",
        const std::string& fragment_entry = "PSMain");

    /**
     * @brief Load shader program from pre-compiled files
     */
    static std::unique_ptr<ShaderProgram> load(
        GPUDevice& device,
        const std::string& vertex_path,
        const std::string& fragment_path,
        const std::string& vertex_entry = "main",
        const std::string& fragment_entry = "main");

    SDL_GPUShader* vertex_shader() const { return vertex_ ? vertex_->handle() : nullptr; }
    SDL_GPUShader* fragment_shader() const { return fragment_ ? fragment_->handle() : nullptr; }

private:
    ShaderProgram() = default;

    std::unique_ptr<GPUShader> vertex_;
    std::unique_ptr<GPUShader> fragment_;
};

} // namespace mmo::gpu
