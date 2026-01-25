#include "shader.hpp"
#include <iostream>
#include <vector>
#include <fstream>
#include <filesystem>
#include <functional>

namespace mmo {

// Static member initialization
std::string Shader::cache_directory_ = "shader_cache";
bool Shader::binary_cache_enabled_ = true;

Shader::~Shader() {
    if (program_) {
        glDeleteProgram(program_);
    }
}

void Shader::set_cache_directory(const std::string& path) {
    cache_directory_ = path;
}

void Shader::enable_binary_cache(bool enabled) {
    binary_cache_enabled_ = enabled;
}

bool Shader::is_binary_cache_supported() {
    GLint num_formats = 0;
    glGetIntegerv(GL_NUM_PROGRAM_BINARY_FORMATS, &num_formats);
    return num_formats > 0;
}

std::string Shader::compute_cache_key(const std::string& vertex_src, const std::string& fragment_src) {
    // Simple hash combining both shader sources
    std::hash<std::string> hasher;
    size_t hash = hasher(vertex_src);
    hash ^= hasher(fragment_src) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
    return std::to_string(hash);
}

bool Shader::load_from_binary_cache(const std::string& cache_key) {
    if (!binary_cache_enabled_ || !is_binary_cache_supported()) {
        return false;
    }
    
    std::string cache_path = cache_directory_ + "/" + cache_key + ".bin";
    std::string format_path = cache_directory_ + "/" + cache_key + ".fmt";
    
    std::ifstream bin_file(cache_path, std::ios::binary | std::ios::ate);
    std::ifstream fmt_file(format_path);
    
    if (!bin_file.is_open() || !fmt_file.is_open()) {
        return false;
    }
    
    // Read format
    GLenum format;
    fmt_file >> format;
    if (fmt_file.fail()) {
        return false;
    }
    
    // Read binary
    std::streamsize size = bin_file.tellg();
    bin_file.seekg(0, std::ios::beg);
    
    std::vector<char> binary(size);
    if (!bin_file.read(binary.data(), size)) {
        return false;
    }
    
    // Create program from binary
    program_ = glCreateProgram();
    glProgramBinary(program_, format, binary.data(), static_cast<GLsizei>(size));
    
    // Check if loading succeeded
    GLint success;
    glGetProgramiv(program_, GL_LINK_STATUS, &success);
    if (!success) {
        glDeleteProgram(program_);
        program_ = 0;
        // Remove invalid cache files
        std::filesystem::remove(cache_path);
        std::filesystem::remove(format_path);
        return false;
    }
    
    return true;
}

void Shader::save_to_binary_cache(const std::string& cache_key) {
    if (!binary_cache_enabled_ || !is_binary_cache_supported() || !program_) {
        return;
    }
    
    // Create cache directory if it doesn't exist
    std::filesystem::create_directories(cache_directory_);
    
    // Get binary size
    GLint binary_length = 0;
    glGetProgramiv(program_, GL_PROGRAM_BINARY_LENGTH, &binary_length);
    if (binary_length <= 0) {
        return;
    }
    
    // Get binary
    std::vector<char> binary(binary_length);
    GLenum format;
    glGetProgramBinary(program_, binary_length, nullptr, &format, binary.data());
    
    // Save binary
    std::string cache_path = cache_directory_ + "/" + cache_key + ".bin";
    std::ofstream bin_file(cache_path, std::ios::binary);
    if (bin_file.is_open()) {
        bin_file.write(binary.data(), binary_length);
    }
    
    // Save format
    std::string format_path = cache_directory_ + "/" + cache_key + ".fmt";
    std::ofstream fmt_file(format_path);
    if (fmt_file.is_open()) {
        fmt_file << format;
    }
}

bool Shader::load(const std::string& vertex_src, const std::string& fragment_src) {
    std::string cache_key = compute_cache_key(vertex_src, fragment_src);
    return load(vertex_src, fragment_src, cache_key);
}

bool Shader::load(const std::string& vertex_src, const std::string& fragment_src,
                  const std::string& cache_name) {
    // Try to load from binary cache first (OpenGL 4.1 feature)
    if (load_from_binary_cache(cache_name)) {
        return true;
    }
    
    // Compile from source
    GLuint vertex_shader = compile_shader(GL_VERTEX_SHADER, vertex_src);
    if (!vertex_shader) return false;
    
    GLuint fragment_shader = compile_shader(GL_FRAGMENT_SHADER, fragment_src);
    if (!fragment_shader) {
        glDeleteShader(vertex_shader);
        return false;
    }
    
    program_ = glCreateProgram();
    
    // Enable program binary retrieval (required for glGetProgramBinary)
    glProgramParameteri(program_, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, GL_TRUE);
    
    glAttachShader(program_, vertex_shader);
    glAttachShader(program_, fragment_shader);
    glLinkProgram(program_);
    
    GLint success;
    glGetProgramiv(program_, GL_LINK_STATUS, &success);
    if (!success) {
        char log[512];
        glGetProgramInfoLog(program_, 512, nullptr, log);
        std::cerr << "Shader link error: " << log << std::endl;
        glDeleteProgram(program_);
        program_ = 0;
    }
    
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);
    
    // Save to binary cache for next time
    if (program_) {
        save_to_binary_cache(cache_name);
    }
    
    return program_ != 0;
}

void Shader::use() const {
    glUseProgram(program_);
}

void Shader::set_mat4(const std::string& name, const glm::mat4& mat) const {
    glUniformMatrix4fv(glGetUniformLocation(program_, name.c_str()), 1, GL_FALSE, glm::value_ptr(mat));
}

void Shader::set_vec2(const std::string& name, const glm::vec2& vec) const {
    glUniform2fv(glGetUniformLocation(program_, name.c_str()), 1, glm::value_ptr(vec));
}

void Shader::set_vec3(const std::string& name, const glm::vec3& vec) const {
    glUniform3fv(glGetUniformLocation(program_, name.c_str()), 1, glm::value_ptr(vec));
}

void Shader::set_vec4(const std::string& name, const glm::vec4& vec) const {
    glUniform4fv(glGetUniformLocation(program_, name.c_str()), 1, glm::value_ptr(vec));
}

void Shader::set_float(const std::string& name, float value) const {
    glUniform1f(glGetUniformLocation(program_, name.c_str()), value);
}

void Shader::set_int(const std::string& name, int value) const {
    glUniform1i(glGetUniformLocation(program_, name.c_str()), value);
}

GLuint Shader::compile_shader(GLenum type, const std::string& source) {
    GLuint shader = glCreateShader(type);
    const char* src = source.c_str();
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);
    
    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[512];
        glGetShaderInfoLog(shader, 512, nullptr, log);
        std::cerr << "Shader compile error (" 
                  << (type == GL_VERTEX_SHADER ? "vertex" : "fragment") 
                  << "): " << log << std::endl;
        glDeleteShader(shader);
        return 0;
    }
    
    return shader;
}

} // namespace mmo
