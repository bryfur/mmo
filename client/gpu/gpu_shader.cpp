#include "gpu_shader.hpp"
#include <SDL3/SDL_log.h>
#include <SDL3_shadercross/SDL_shadercross.h>
#include <fstream>
#include <sstream>

namespace mmo::gpu {

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

std::unique_ptr<GPUShader> GPUShader::load_spirv(
    GPUDevice& device,
    const std::string& path,
    ShaderStage stage,
    const std::string& entry_point,
    const ShaderResources& resources) {

    std::vector<uint8_t> bytecode = read_file(path);
    if (bytecode.empty()) {
        return nullptr;
    }

    auto shader = create_from_spirv(device, bytecode, stage, entry_point, resources);
    if (shader) {
        SDL_Log("GPUShader::load_spirv: Loaded '%s' successfully", path.c_str());
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

    if (spirv_bytecode.empty()) {
        SDL_Log("GPUShader::create_from_spirv: Empty bytecode");
        return nullptr;
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

    // Compile SPIRV to backend shader (Vulkan uses SPIRV directly,
    // Metal/D3D12 get transpiled via SPIRV-Cross)
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

// =============================================================================
// ShaderManager Implementation
// =============================================================================

ShaderManager::ShaderManager(GPUDevice& device)
    : device_(device) {
    // Initialize the cross-compilation system
    GPUShader::init_compiler();
}

ShaderManager::~ShaderManager() {
    clear_cache();
}

std::string ShaderManager::make_cache_key(const std::string& path,
                                           ShaderStage stage,
                                           const std::string& entry_point) const {
    // Use length-prefixed fields to avoid collisions
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

    // Check cache
    auto it = cache_.find(key);
    if (it != cache_.end()) {
        return it->second.get();
    }

    // Load shader
    auto shader = GPUShader::load_spirv(device_, path, stage, entry_point, resources);
    if (!shader) {
        SDL_Log("ShaderManager: Failed to load '%s'", path.c_str());
        return nullptr;
    }

    // Cache and return
    GPUShader* result = shader.get();
    cache_[key] = std::move(shader);
    return result;
}

void ShaderManager::clear_cache() {
    cache_.clear();
}

bool ShaderManager::reload(const std::string& path) {
    // Find and remove all cache entries for this path
    auto matches_path = [&path](const std::string& key) {
        size_t colon_pos = key.find(':');
        if (colon_pos == std::string::npos) return false;
        size_t path_len = std::stoull(key.substr(0, colon_pos));
        std::string key_path = key.substr(colon_pos + 1, path_len);
        return key_path == path;
    };

    std::vector<std::string> keys_to_remove;
    for (const auto& [key, _] : cache_) {
        if (matches_path(key)) {
            keys_to_remove.push_back(key);
        }
    }

    for (const auto& key : keys_to_remove) {
        cache_.erase(key);
    }

    SDL_Log("ShaderManager: Marked '%s' for reload (%zu variants)",
            path.c_str(), keys_to_remove.size());
    return !keys_to_remove.empty();
}

int ShaderManager::reload_all() {
    int count = static_cast<int>(cache_.size());
    clear_cache();
    SDL_Log("ShaderManager: Cleared %d shaders for reload", count);
    return count;
}

// =============================================================================
// ShaderProgram Implementation
// =============================================================================

std::unique_ptr<ShaderProgram> ShaderProgram::load(
    GPUDevice& device,
    const std::string& vertex_path,
    const std::string& fragment_path,
    const std::string& vertex_entry,
    const std::string& fragment_entry) {

    auto program = std::unique_ptr<ShaderProgram>(new ShaderProgram());

    program->vertex_ = GPUShader::load_spirv(device, vertex_path,
                                              ShaderStage::Vertex, vertex_entry);
    if (!program->vertex_) {
        SDL_Log("ShaderProgram::load: Failed to load vertex shader '%s'",
                vertex_path.c_str());
        return nullptr;
    }

    program->fragment_ = GPUShader::load_spirv(device, fragment_path,
                                                ShaderStage::Fragment, fragment_entry);
    if (!program->fragment_) {
        SDL_Log("ShaderProgram::load: Failed to load fragment shader '%s'",
                fragment_path.c_str());
        return nullptr;
    }

    return program;
}

} // namespace mmo::gpu
