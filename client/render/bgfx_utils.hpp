#pragma once

#include <bgfx/bgfx.h>
#include <string>

namespace mmo {

/**
 * Utility functions for bgfx operations shared across renderers.
 */
namespace bgfx_utils {

/**
 * Load a compiled bgfx shader binary from the shaders/ directory.
 * @param name Shader name without extension (e.g., "terrain_vs")
 * @return Valid shader handle or BGFX_INVALID_HANDLE on failure
 */
bgfx::ShaderHandle load_shader(const char* name);

/**
 * Load a shader program from vertex and fragment shader names.
 * @param vs_name Vertex shader name without extension
 * @param fs_name Fragment shader name without extension
 * @return Valid program handle or BGFX_INVALID_HANDLE on failure
 */
bgfx::ProgramHandle load_program(const char* vs_name, const char* fs_name);

/**
 * Create a bgfx texture from an SDL surface loaded via SDL_image.
 * @param path Path to the image file
 * @param flags Texture sampler flags
 * @return Valid texture handle or BGFX_INVALID_HANDLE on failure
 */
bgfx::TextureHandle load_texture(const char* path, uint64_t flags = BGFX_SAMPLER_MIN_ANISOTROPIC);

} // namespace bgfx_utils

} // namespace mmo
