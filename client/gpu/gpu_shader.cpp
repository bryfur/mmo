#include "gpu_shader.hpp"
#include <SDL3/SDL_log.h>
#include <SDL3_shadercross/SDL_shadercross.h>
#include <fstream>
#include <sstream>
#include <iomanip>

namespace mmo::gpu {

// Anonymous namespace for file-local helpers
namespace {

std::string load_text_file(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        SDL_Log("load_text_file: Failed to open '%s'", path.c_str());
        return {};
    }

    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

} // anonymous namespace

// Static member initialization
bool GPUShader::s_compiler_initialized = false;

// =============================================================================
// GPUShader Implementation
// =============================================================================

GPUShader::~GPUShader() {
    if (device_ && shader_) {
        device_->release_shader(shader_);
    }
}

GPUShader::GPUShader(GPUShader&& other) noexcept
    : device_(other.device_)
    , shader_(other.shader_)
    , stage_(other.stage_) {
    other.device_ = nullptr;
    other.shader_ = nullptr;
}

GPUShader& GPUShader::operator=(GPUShader&& other) noexcept {
    if (this != &other) {
        if (device_ && shader_) {
            device_->release_shader(shader_);
        }

        device_ = other.device_;
        shader_ = other.shader_;
        stage_ = other.stage_;

        other.device_ = nullptr;
        other.shader_ = nullptr;
    }
    return *this;
}

bool GPUShader::init_compiler() {
    if (s_compiler_initialized) {
        return true;
    }

    if (!SDL_ShaderCross_Init()) {
        SDL_Log("GPUShader::init_compiler: Failed to initialize SDL_shadercross: %s", 
                SDL_GetError());
        return false;
    }

    s_compiler_initialized = true;
    SDL_Log("GPUShader::init_compiler: SDL_shadercross initialized");
    return true;
}

void GPUShader::shutdown_compiler() {
    if (s_compiler_initialized) {
        SDL_ShaderCross_Quit();
        s_compiler_initialized = false;
        SDL_Log("GPUShader::shutdown_compiler: SDL_shadercross shutdown");
    }
}

bool GPUShader::is_compiler_available() {
    return s_compiler_initialized;
}

std::unique_ptr<GPUShader> GPUShader::compile_from_hlsl(
    GPUDevice& device,
    const std::string& hlsl_source,
    ShaderStage stage,
    const std::string& entry_point,
    const ShaderResources& resources) {
    
    if (!s_compiler_initialized) {
        if (!init_compiler()) {
            return nullptr;
        }
    }

    auto shader = std::unique_ptr<GPUShader>(new GPUShader());
    shader->device_ = &device;
    shader->stage_ = stage;

    // Step 1: Set up HLSL info and compile to SPIRV
    SDL_ShaderCross_HLSL_Info hlsl_info = {};
    hlsl_info.source = hlsl_source.c_str();
    hlsl_info.entrypoint = entry_point.c_str();
    hlsl_info.shader_stage = (stage == ShaderStage::Vertex)
        ? SDL_SHADERCROSS_SHADERSTAGE_VERTEX
        : SDL_SHADERCROSS_SHADERSTAGE_FRAGMENT;
    hlsl_info.include_dir = nullptr;
    hlsl_info.defines = nullptr;
    hlsl_info.props = 0;

    // Compile HLSL to SPIRV bytecode
    size_t spirv_size = 0;
    void* spirv_bytecode = SDL_ShaderCross_CompileSPIRVFromHLSL(&hlsl_info, &spirv_size);
    if (!spirv_bytecode) {
        SDL_Log("GPUShader::compile_from_hlsl: HLSL->SPIRV compilation failed: %s", SDL_GetError());
        return nullptr;
    }

    // Step 2: Reflect to get resource info (or use provided resources)
    SDL_ShaderCross_GraphicsShaderResourceInfo resource_info = {};
    resource_info.num_samplers = resources.num_samplers;
    resource_info.num_storage_textures = resources.num_storage_textures;
    resource_info.num_storage_buffers = resources.num_storage_buffers;
    resource_info.num_uniform_buffers = resources.num_uniform_buffers;

    // Step 3: Set up SPIRV info for final compilation
    SDL_ShaderCross_SPIRV_Info spirv_info = {};
    spirv_info.bytecode = static_cast<const uint8_t*>(spirv_bytecode);
    spirv_info.bytecode_size = spirv_size;
    spirv_info.entrypoint = entry_point.c_str();
    spirv_info.shader_stage = hlsl_info.shader_stage;
    spirv_info.props = 0;

    // Step 4: Compile SPIRV to final GPU shader
    shader->shader_ = SDL_ShaderCross_CompileGraphicsShaderFromSPIRV(
        device.handle(),
        &spirv_info,
        &resource_info,
        0  // props
    );

    // Free the intermediate SPIRV bytecode
    SDL_free(spirv_bytecode);

    if (!shader->shader_) {
        SDL_Log("GPUShader::compile_from_hlsl: SPIRV->GPU shader compilation failed: %s", SDL_GetError());
        return nullptr;
    }

    return shader;
}

std::unique_ptr<GPUShader> GPUShader::load_hlsl(
    GPUDevice& device,
    const std::string& path,
    ShaderStage stage,
    const std::string& entry_point,
    const ShaderResources& resources) {
    
    std::string source = read_text_file(path);
    if (source.empty()) {
        SDL_Log("GPUShader::load_hlsl: Failed to read '%s'", path.c_str());
        return nullptr;
    }

    auto shader = compile_from_hlsl(device, source, stage, entry_point, resources);
    if (shader) {
        SDL_Log("GPUShader::load_hlsl: Compiled '%s' successfully", path.c_str());
    }
    return shader;
}

std::unique_ptr<GPUShader> GPUShader::compile_from_hlsl_with_spirv(
    GPUDevice& device,
    const std::string& hlsl_source,
    ShaderStage stage,
    const std::string& entry_point,
    const ShaderResources& resources,
    std::vector<uint8_t>* spirv_out) {
    
    if (!s_compiler_initialized) {
        if (!init_compiler()) {
            return nullptr;
        }
    }

    auto shader = std::unique_ptr<GPUShader>(new GPUShader());
    shader->device_ = &device;
    shader->stage_ = stage;

    // Set up HLSL info and compile to SPIRV
    SDL_ShaderCross_HLSL_Info hlsl_info = {};
    hlsl_info.source = hlsl_source.c_str();
    hlsl_info.entrypoint = entry_point.c_str();
    hlsl_info.shader_stage = (stage == ShaderStage::Vertex)
        ? SDL_SHADERCROSS_SHADERSTAGE_VERTEX
        : SDL_SHADERCROSS_SHADERSTAGE_FRAGMENT;
    hlsl_info.include_dir = nullptr;
    hlsl_info.defines = nullptr;
    hlsl_info.props = 0;

    // Compile HLSL to SPIRV bytecode
    size_t spirv_size = 0;
    void* spirv_bytecode = SDL_ShaderCross_CompileSPIRVFromHLSL(&hlsl_info, &spirv_size);
    if (!spirv_bytecode) {
        SDL_Log("GPUShader::compile_from_hlsl_with_spirv: HLSL->SPIRV failed: %s", SDL_GetError());
        return nullptr;
    }

    // Output SPIRV for caching if requested
    if (spirv_out) {
        spirv_out->assign(
            static_cast<uint8_t*>(spirv_bytecode),
            static_cast<uint8_t*>(spirv_bytecode) + spirv_size
        );
    }

    // Set up resource info
    SDL_ShaderCross_GraphicsShaderResourceInfo resource_info = {};
    resource_info.num_samplers = resources.num_samplers;
    resource_info.num_storage_textures = resources.num_storage_textures;
    resource_info.num_storage_buffers = resources.num_storage_buffers;
    resource_info.num_uniform_buffers = resources.num_uniform_buffers;

    // Set up SPIRV info for final compilation
    SDL_ShaderCross_SPIRV_Info spirv_info = {};
    spirv_info.bytecode = static_cast<const uint8_t*>(spirv_bytecode);
    spirv_info.bytecode_size = spirv_size;
    spirv_info.entrypoint = entry_point.c_str();
    spirv_info.shader_stage = hlsl_info.shader_stage;
    spirv_info.props = 0;

    // Compile SPIRV to final GPU shader
    shader->shader_ = SDL_ShaderCross_CompileGraphicsShaderFromSPIRV(
        device.handle(),
        &spirv_info,
        &resource_info,
        0
    );

    SDL_free(spirv_bytecode);

    if (!shader->shader_) {
        SDL_Log("GPUShader::compile_from_hlsl_with_spirv: SPIRV->GPU failed: %s", SDL_GetError());
        return nullptr;
    }

    return shader;
}

std::unique_ptr<GPUShader> GPUShader::create_from_spirv(
    GPUDevice& device,
    const std::vector<uint8_t>& spirv_bytecode,
    ShaderStage stage,
    const std::string& entry_point,
    const ShaderResources& resources) {
    
    if (!s_compiler_initialized) {
        if (!init_compiler()) {
            return nullptr;
        }
    }

    auto shader = std::unique_ptr<GPUShader>(new GPUShader());
    shader->device_ = &device;
    shader->stage_ = stage;

    // Set up resource info
    SDL_ShaderCross_GraphicsShaderResourceInfo resource_info = {};
    resource_info.num_samplers = resources.num_samplers;
    resource_info.num_storage_textures = resources.num_storage_textures;
    resource_info.num_storage_buffers = resources.num_storage_buffers;
    resource_info.num_uniform_buffers = resources.num_uniform_buffers;

    // Set up SPIRV info
    SDL_ShaderCross_SPIRV_Info spirv_info = {};
    spirv_info.bytecode = spirv_bytecode.data();
    spirv_info.bytecode_size = spirv_bytecode.size();
    spirv_info.entrypoint = entry_point.c_str();
    spirv_info.shader_stage = (stage == ShaderStage::Vertex)
        ? SDL_SHADERCROSS_SHADERSTAGE_VERTEX
        : SDL_SHADERCROSS_SHADERSTAGE_FRAGMENT;
    spirv_info.props = 0;

    // Compile SPIRV to final GPU shader
    shader->shader_ = SDL_ShaderCross_CompileGraphicsShaderFromSPIRV(
        device.handle(),
        &spirv_info,
        &resource_info,
        0
    );

    if (!shader->shader_) {
        SDL_Log("GPUShader::create_from_spirv: SPIRV->GPU failed: %s", SDL_GetError());
        return nullptr;
    }

    return shader;
}

SDL_GPUShaderFormat GPUShader::detect_format_from_path(const std::string& path) {
    size_t dot_pos = path.rfind('.');
    if (dot_pos == std::string::npos) {
        return SDL_GPU_SHADERFORMAT_INVALID;
    }

    std::string ext = path.substr(dot_pos);
    
    // Handle double extensions like .vert.spv
    size_t second_dot = path.rfind('.', dot_pos - 1);
    if (second_dot != std::string::npos) {
        ext = path.substr(second_dot);
    }

    if (ext.find(".spv") != std::string::npos) {
        return SDL_GPU_SHADERFORMAT_SPIRV;
    } else if (ext.find(".metallib") != std::string::npos || ext.find(".metal") != std::string::npos) {
        return SDL_GPU_SHADERFORMAT_METALLIB;
    } else if (ext.find(".dxil") != std::string::npos) {
        return SDL_GPU_SHADERFORMAT_DXIL;
    }

    return SDL_GPU_SHADERFORMAT_INVALID;
}

std::vector<uint8_t> GPUShader::read_file(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        SDL_Log("GPUShader::read_file: Failed to open '%s'", path.c_str());
        return {};
    }

    std::streamsize size = file.tellg();
    if (size <= 0) {
        SDL_Log("GPUShader::read_file: Empty file '%s'", path.c_str());
        return {};
    }

    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> buffer(static_cast<size_t>(size));

    if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
        SDL_Log("GPUShader::read_file: Failed to read '%s'", path.c_str());
        return {};
    }

    return buffer;
}

std::string GPUShader::read_text_file(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        SDL_Log("GPUShader::read_text_file: Failed to open '%s'", path.c_str());
        return {};
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

std::unique_ptr<GPUShader> GPUShader::load_from_file(
    GPUDevice& device,
    const std::string& path,
    ShaderStage stage,
    const std::string& entry_point,
    const ShaderResources& resources) {
    
    SDL_GPUShaderFormat format = detect_format_from_path(path);
    if (format == SDL_GPU_SHADERFORMAT_INVALID) {
        SDL_Log("GPUShader::load_from_file: Unknown format for '%s'", path.c_str());
        return nullptr;
    }

    std::vector<uint8_t> bytecode = read_file(path);
    if (bytecode.empty()) {
        return nullptr;
    }

    return create_from_bytecode(device, bytecode, stage, format, entry_point, resources);
}

std::unique_ptr<GPUShader> GPUShader::create_from_bytecode(
    GPUDevice& device,
    const std::vector<uint8_t>& bytecode,
    ShaderStage stage,
    SDL_GPUShaderFormat format,
    const std::string& entry_point,
    const ShaderResources& resources) {
    
    if (bytecode.empty()) {
        SDL_Log("GPUShader::create_from_bytecode: Empty bytecode");
        return nullptr;
    }

    auto shader = std::unique_ptr<GPUShader>(new GPUShader());
    shader->device_ = &device;
    shader->stage_ = stage;

    SDL_GPUShaderCreateInfo shader_info = {};
    shader_info.code = bytecode.data();
    shader_info.code_size = bytecode.size();
    shader_info.entrypoint = entry_point.c_str();
    shader_info.format = format;
    shader_info.stage = (stage == ShaderStage::Vertex) 
                         ? SDL_GPU_SHADERSTAGE_VERTEX 
                         : SDL_GPU_SHADERSTAGE_FRAGMENT;
    shader_info.num_samplers = resources.num_samplers;
    shader_info.num_storage_textures = resources.num_storage_textures;
    shader_info.num_storage_buffers = resources.num_storage_buffers;
    shader_info.num_uniform_buffers = resources.num_uniform_buffers;

    shader->shader_ = device.create_shader(shader_info);
    if (!shader->shader_) {
        SDL_Log("GPUShader::create_from_bytecode: Failed to create shader: %s", SDL_GetError());
        return nullptr;
    }

    return shader;
}

// =============================================================================
// ShaderDiskCache Implementation
// =============================================================================

ShaderDiskCache::ShaderDiskCache(const std::filesystem::path& cache_dir)
    : cache_dir_(cache_dir) {
    if (!cache_dir_.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(cache_dir_, ec);
        if (ec) {
            SDL_Log("ShaderDiskCache: Failed to create cache directory '%s': %s",
                    cache_dir_.string().c_str(), ec.message().c_str());
            enabled_ = false;
        }
    } else {
        enabled_ = false;
    }
}

void ShaderDiskCache::clear() {
    if (cache_dir_.empty()) return;
    
    std::error_code ec;
    std::filesystem::directory_iterator it(cache_dir_, ec);
    if (ec) {
        SDL_Log("ShaderDiskCache: Failed to iterate cache directory '%s': %s",
                cache_dir_.string().c_str(), ec.message().c_str());
        return;
    }

    for (const auto& entry : it) {
        std::filesystem::remove(entry.path(), ec);
        if (ec) {
            SDL_Log("ShaderDiskCache: Failed to remove cache file '%s': %s",
                    entry.path().string().c_str(), ec.message().c_str());
            ec.clear();
        }
    }
    SDL_Log("ShaderDiskCache: Cleared cache directory");
}

std::string ShaderDiskCache::compute_hash(const std::string& source,
                                           ShaderStage stage,
                                           const std::string& entry_point) {
    // Hash combining source, stage, and entry point using FNV-1a algorithm.
    // NOTE: FNV-1a is fast but not cryptographically secure. For production
    // systems where cache integrity is critical, consider using SHA-256.
    // However, FNV-1a is sufficient for shader caching since:
    //   1. Collisions are rare for typical shader code
    //   2. A collision only causes unnecessary recompilation, not corruption
    uint64_t hash = 14695981039346656037ULL; // FNV offset basis
    const uint64_t prime = 1099511628211ULL;

    for (char c : source) {
        hash ^= static_cast<uint64_t>(c);
        hash *= prime;
    }
    
    hash ^= static_cast<uint64_t>(stage);
    hash *= prime;
    
    for (char c : entry_point) {
        hash ^= static_cast<uint64_t>(c);
        hash *= prime;
    }

    // Convert to hex string
    std::ostringstream oss;
    oss << std::hex << std::setfill('0') << std::setw(16) << hash;
    return oss.str();
}

std::string ShaderDiskCache::format_extension(SDL_GPUShaderFormat format) {
    switch (format) {
        case SDL_GPU_SHADERFORMAT_SPIRV: return ".spv";
        case SDL_GPU_SHADERFORMAT_METALLIB: return ".metal";
        case SDL_GPU_SHADERFORMAT_DXIL: return ".dxil";
        default: return ".bin";
    }
}

std::filesystem::path ShaderDiskCache::get_cache_path(const std::string& hash,
                                                        SDL_GPUShaderFormat format) const {
    return cache_dir_ / (hash + format_extension(format));
}

std::vector<uint8_t> ShaderDiskCache::get(const std::string& source_hash,
                                           SDL_GPUShaderFormat format) {
    if (!enabled_ || cache_dir_.empty()) {
        return {};
    }

    auto cache_path = get_cache_path(source_hash, format);
    
    std::ifstream file(cache_path, std::ios::binary | std::ios::ate);
    if (!file) {
        return {};
    }

    std::streamsize size = file.tellg();
    if (size <= 0) {
        return {};
    }

    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> buffer(static_cast<size_t>(size));

    if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
        return {};
    }

    SDL_Log("ShaderDiskCache: Cache hit for %s", source_hash.c_str());
    return buffer;
}

void ShaderDiskCache::put(const std::string& source_hash,
                          SDL_GPUShaderFormat format,
                          const std::vector<uint8_t>& bytecode) {
    if (!enabled_ || cache_dir_.empty() || bytecode.empty()) {
        return;
    }

    auto cache_path = get_cache_path(source_hash, format);
    
    std::ofstream file(cache_path, std::ios::binary);
    if (!file) {
        SDL_Log("ShaderDiskCache: Failed to write cache file '%s'", 
                cache_path.string().c_str());
        return;
    }

    file.write(reinterpret_cast<const char*>(bytecode.data()), 
               static_cast<std::streamsize>(bytecode.size()));
    
    SDL_Log("ShaderDiskCache: Cached shader %s", source_hash.c_str());
}

// =============================================================================
// ShaderManager Implementation
// =============================================================================

ShaderManager::ShaderManager(GPUDevice& device, const std::string& cache_dir)
    : device_(device) {
    if (!cache_dir.empty()) {
        disk_cache_ = std::make_unique<ShaderDiskCache>(cache_dir);
    }
    
    // Initialize the compiler
    GPUShader::init_compiler();
}

ShaderManager::~ShaderManager() {
    clear_memory_cache();
}

std::string ShaderManager::make_cache_key(const std::string& path,
                                           ShaderStage stage,
                                           const std::string& entry_point) const {
    // Use length-prefixed fields to avoid collisions when delimiters appear in values.
    // e.g., path "a|b.hlsl" won't collide with path "a" + entry "b.hlsl"
    std::ostringstream oss;
    oss << path.size() << ':' << path
        << '|' << static_cast<int>(stage) << '|'
        << entry_point.size() << ':' << entry_point;
    return oss.str();
}

GPUShader* ShaderManager::get_vertex(const std::string& path,
                                      const std::string& entry_point,
                                      const ShaderResources& resources) {
    return get(path, ShaderStage::Vertex, entry_point, resources);
}

GPUShader* ShaderManager::get_fragment(const std::string& path,
                                        const std::string& entry_point,
                                        const ShaderResources& resources) {
    return get(path, ShaderStage::Fragment, entry_point, resources);
}

GPUShader* ShaderManager::get(const std::string& path,
                               ShaderStage stage,
                               const std::string& entry_point,
                               const ShaderResources& resources) {
    std::string key = make_cache_key(path, stage, entry_point);

    // Check memory cache
    auto it = memory_cache_.find(key);
    if (it != memory_cache_.end()) {
        return it->second.get();
    }

    // Load with disk caching
    return load_with_cache(path, stage, entry_point, resources);
}

GPUShader* ShaderManager::load_with_cache(const std::string& path,
                                           ShaderStage stage,
                                           const std::string& entry_point,
                                           const ShaderResources& resources) {
    std::string key = make_cache_key(path, stage, entry_point);

    // Read source file
    std::string source = load_text_file(path);
    if (source.empty()) {
        SDL_Log("ShaderManager: Failed to read '%s'", path.c_str());
        return nullptr;
    }

    // Store source for potential reload
    path_to_source_[path] = source;

    // Compute hash for disk cache lookup
    std::string source_hash = ShaderDiskCache::compute_hash(source, stage, entry_point);

    // Try disk cache first - load cached SPIRV bytecode
    std::vector<uint8_t> cached_spirv;
    if (disk_cache_ && disk_cache_->is_enabled()) {
        cached_spirv = disk_cache_->get(source_hash, SDL_GPU_SHADERFORMAT_SPIRV);
    }

    std::unique_ptr<GPUShader> shader;

    if (!cached_spirv.empty()) {
        // Cache hit! Compile from cached SPIRV (skips HLSL->SPIRV step)
        SDL_Log("ShaderManager: Cache hit for '%s'", path.c_str());
        shader = GPUShader::create_from_spirv(device_, cached_spirv, stage, entry_point, resources);
    } else {
        // Cache miss - compile from HLSL and cache the SPIRV
        SDL_Log("ShaderManager: Cache miss for '%s', compiling...", path.c_str());
        
        std::vector<uint8_t> spirv_bytecode;
        shader = GPUShader::compile_from_hlsl_with_spirv(
            device_, source, stage, entry_point, resources, &spirv_bytecode);
        
        // Store SPIRV in disk cache for next time
        if (shader && disk_cache_ && disk_cache_->is_enabled() && !spirv_bytecode.empty()) {
            disk_cache_->put(source_hash, SDL_GPU_SHADERFORMAT_SPIRV, spirv_bytecode);
            SDL_Log("ShaderManager: Cached SPIRV for '%s'", path.c_str());
        }
    }

    if (!shader) {
        SDL_Log("ShaderManager: Failed to compile '%s'", path.c_str());
        return nullptr;
    }

    // Cache in memory
    GPUShader* result = shader.get();
    memory_cache_[key] = std::move(shader);

    SDL_Log("ShaderManager: Loaded and cached '%s'", path.c_str());
    return result;
}

void ShaderManager::clear_memory_cache() {
    memory_cache_.clear();
    path_to_source_.clear();
}

void ShaderManager::clear_disk_cache() {
    if (disk_cache_) {
        disk_cache_->clear();
    }
}

void ShaderManager::set_disk_cache_enabled(bool enabled) {
    if (disk_cache_) {
        disk_cache_->set_enabled(enabled);
    }
}

bool ShaderManager::reload(const std::string& path) {
    // Match keys that start with the exact path, followed by a delimiter.
    // This prevents "shaders/model.hlsl" from matching "shaders/model_skinned.hlsl"
    auto matches_path = [](const std::string& key, const std::string& target_path) {
        // Key format is: "<path_len>:<path>|<stage>|<entry_len>:<entry>"
        // Extract the path portion and compare exactly
        size_t colon_pos = key.find(':');
        if (colon_pos == std::string::npos) return false;
        
        size_t path_len = std::stoull(key.substr(0, colon_pos));
        std::string key_path = key.substr(colon_pos + 1, path_len);
        return key_path == target_path;
    };

    std::vector<std::string> keys_to_remove;

    // Match keys that start with the exact path, optionally followed by a delimiter.
    auto matches_path = [](const std::string& key, const std::string& path) {
        if (key.size() < path.size()) {
            return false;
        }

        if (key.compare(0, path.size(), path) != 0) {
            return false;
        }

        if (key.size() == path.size()) {
            return true;
        }

        const char next = key[path.size()];
        // Allow known delimiters used to append variant or metadata info to the path.
        return next == '|' || next == ':' || next == '#';
    };

    for (const auto& [key, shader] : memory_cache_) {
        if (matches_path(key, path)) {
            keys_to_remove.push_back(key);
        }
    }

    // Remove from cache (they'll be reloaded on next get())
    for (const auto& key : keys_to_remove) {
        memory_cache_.erase(key);
    }

    SDL_Log("ShaderManager: Marked '%s' for reload (%zu variants)", 
            path.c_str(), keys_to_remove.size());
    return !keys_to_remove.empty();
}

int ShaderManager::reload_all() {
    int count = static_cast<int>(memory_cache_.size());
    clear_memory_cache();
    SDL_Log("ShaderManager: Cleared %d shaders for reload", count);
    return count;
}

// =============================================================================
// ShaderProgram Implementation
// =============================================================================

std::unique_ptr<ShaderProgram> ShaderProgram::load_hlsl(
    GPUDevice& device,
    const std::string& vertex_path,
    const std::string& fragment_path,
    const std::string& vertex_entry,
    const std::string& fragment_entry) {
    
    auto program = std::unique_ptr<ShaderProgram>(new ShaderProgram());

    program->vertex_ = GPUShader::load_hlsl(device, vertex_path, 
                                             ShaderStage::Vertex, vertex_entry);
    if (!program->vertex_) {
        SDL_Log("ShaderProgram::load_hlsl: Failed to load vertex shader '%s'", 
                vertex_path.c_str());
        return nullptr;
    }

    program->fragment_ = GPUShader::load_hlsl(device, fragment_path,
                                               ShaderStage::Fragment, fragment_entry);
    if (!program->fragment_) {
        SDL_Log("ShaderProgram::load_hlsl: Failed to load fragment shader '%s'", 
                fragment_path.c_str());
        return nullptr;
    }

    return program;
}

std::unique_ptr<ShaderProgram> ShaderProgram::load(
    GPUDevice& device,
    const std::string& vertex_path,
    const std::string& fragment_path,
    const std::string& vertex_entry,
    const std::string& fragment_entry) {
    
    auto program = std::unique_ptr<ShaderProgram>(new ShaderProgram());

    program->vertex_ = GPUShader::load_from_file(device, vertex_path, 
                                                  ShaderStage::Vertex, vertex_entry);
    if (!program->vertex_) {
        SDL_Log("ShaderProgram::load: Failed to load vertex shader '%s'", 
                vertex_path.c_str());
        return nullptr;
    }

    program->fragment_ = GPUShader::load_from_file(device, fragment_path,
                                                    ShaderStage::Fragment, fragment_entry);
    if (!program->fragment_) {
        SDL_Log("ShaderProgram::load: Failed to load fragment shader '%s'", 
                fragment_path.c_str());
        return nullptr;
    }

    return program;
}

} // namespace mmo::gpu
