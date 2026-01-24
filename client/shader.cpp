#include "shader.hpp"
#include <iostream>
#include <vector>

namespace mmo {

Shader::~Shader() {
    if (program_) {
        glDeleteProgram(program_);
    }
}

bool Shader::load(const std::string& vertex_src, const std::string& fragment_src) {
    GLuint vertex_shader = compile_shader(GL_VERTEX_SHADER, vertex_src);
    if (!vertex_shader) return false;
    
    GLuint fragment_shader = compile_shader(GL_FRAGMENT_SHADER, fragment_src);
    if (!fragment_shader) {
        glDeleteShader(vertex_shader);
        return false;
    }
    
    program_ = glCreateProgram();
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
