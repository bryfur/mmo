# Shader compilation rules for SDL3 GPU
# This CMakeLists.txt handles compiling HLSL shaders to multiple backends

# Note: SDL_shadercross integration is planned for future tasks
# For now, this file documents the intended shader compilation setup

# Find SDL_shadercross (when available)
# find_package(SDL_shadercross QUIET)

# Shader source files
set(SHADER_SOURCES
    src/model.vert.hlsl
    src/model.frag.hlsl
    src/skinned_model.vert.hlsl
    src/skinned_model.frag.hlsl
    src/terrain.vert.hlsl
    src/terrain.frag.hlsl
    src/skybox.vert.hlsl
    src/skybox.frag.hlsl
    src/ui.vert.hlsl
    src/ui.frag.hlsl
    src/billboard.vert.hlsl
    src/billboard.frag.hlsl
    src/shadow.vert.hlsl
    src/shadow.frag.hlsl
    src/grass.vert.hlsl
    src/grass.frag.hlsl
    src/effect.vert.hlsl
    src/effect.frag.hlsl
    src/text.vert.hlsl
    src/text.frag.hlsl
)

# Output directories
set(SPIRV_OUTPUT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/compiled/spirv)
set(METAL_OUTPUT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/compiled/metal)
set(DXIL_OUTPUT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/compiled/dxil)

# Create output directories
file(MAKE_DIRECTORY ${SPIRV_OUTPUT_DIR})
file(MAKE_DIRECTORY ${METAL_OUTPUT_DIR})
file(MAKE_DIRECTORY ${DXIL_OUTPUT_DIR})

# Placeholder for shader compilation commands
# These will be implemented when SDL_shadercross is integrated
# 
# Example compile command:
# add_custom_command(
#     OUTPUT ${SPIRV_OUTPUT_DIR}/model.vert.spv
#     COMMAND shadercross -s HLSL -d SPIRV -e VSMain 
#             -i ${CMAKE_CURRENT_SOURCE_DIR}/src/model.vert.hlsl 
#             -o ${SPIRV_OUTPUT_DIR}/model.vert.spv
#     DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/model.vert.hlsl
#     COMMENT "Compiling model.vert.hlsl to SPIR-V"
# )
