# Shader compilation - HLSL to SPIRV at build time

# Fetch SDL_shadercross for shader compilation
# Provides: shadercross CLI (build-time), SDL3_shadercross library (runtime)
include(FetchContent)
FetchContent_Declare(
    SDL_shadercross
    GIT_REPOSITORY https://github.com/libsdl-org/SDL_shadercross.git
    GIT_TAG 7b7365a86611b2a7b6462e521cf1c43a037d0970
    GIT_SUBMODULES_RECURSE TRUE
)
set(SDLSHADERCROSS_CLI ON CACHE BOOL "" FORCE)
set(SDLSHADERCROSS_SHARED OFF CACHE BOOL "" FORCE)
set(SDLSHADERCROSS_STATIC ON CACHE BOOL "" FORCE)
set(SDLSHADERCROSS_INSTALL OFF CACHE BOOL "" FORCE)
set(SDLSHADERCROSS_VENDORED ON CACHE BOOL "" FORCE)
set(SDLSHADERCROSS_SPIRVCROSS_SHARED ON CACHE BOOL "" FORCE)
set(SDLSHADERCROSS_DXC ON CACHE BOOL "" FORCE)
set(CMAKE_POSITION_INDEPENDENT_CODE ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(SDL_shadercross)

# Find system shadercross or use the one we just built
find_program(SHADERCROSS_EXECUTABLE shadercross)
if(NOT SHADERCROSS_EXECUTABLE)
    set(SHADERCROSS_EXECUTABLE $<TARGET_FILE:shadercross>)
    set(SHADERCROSS_DEPENDS shadercross)
    message(STATUS "Using built shadercross CLI")
else()
    set(SHADERCROSS_DEPENDS "")
    message(STATUS "Found system shadercross: ${SHADERCROSS_EXECUTABLE}")
endif()

# Output directory for compiled shaders
set(SHADER_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/../shaders")
file(MAKE_DIRECTORY "${SHADER_OUTPUT_DIR}")

# Collect all HLSL source files (CONFIGURE_DEPENDS re-runs CMake when files change)
file(GLOB_RECURSE SHADER_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.hlsl"
)

set(COMPILED_SHADERS "")

foreach(SHADER_SOURCE IN LISTS SHADER_SOURCES)
    cmake_path(GET SHADER_SOURCE FILENAME SHADER_NAME)

    # Determine shader stage and entry point from filename convention:
    #   name.vert.hlsl -> vertex shader (VSMain)
    #   name.frag.hlsl -> fragment shader (PSMain)
    if(SHADER_NAME MATCHES "\\.vert\\.hlsl$")
        set(SHADER_STAGE "vertex")
        set(ENTRY_POINT "VSMain")
        string(REPLACE ".vert.hlsl" ".vert.spv" OUTPUT_NAME "${SHADER_NAME}")
    elseif(SHADER_NAME MATCHES "\\.frag\\.hlsl$")
        set(SHADER_STAGE "fragment")
        set(ENTRY_POINT "PSMain")
        string(REPLACE ".frag.hlsl" ".frag.spv" OUTPUT_NAME "${SHADER_NAME}")
    else()
        message(WARNING "Unknown shader type: ${SHADER_NAME}, skipping")
        continue()
    endif()

    set(OUTPUT_PATH "${SHADER_OUTPUT_DIR}/${OUTPUT_NAME}")

    add_custom_command(
        OUTPUT "${OUTPUT_PATH}"
        COMMAND ${SHADERCROSS_EXECUTABLE}
            "${SHADER_SOURCE}"
            -s HLSL
            -d SPIRV
            -t ${SHADER_STAGE}
            -e ${ENTRY_POINT}
            -o "${OUTPUT_PATH}"
        DEPENDS "${SHADER_SOURCE}" ${SHADERCROSS_DEPENDS}
        COMMENT "Compiling ${SHADER_NAME}"
        VERBATIM
    )

    list(APPEND COMPILED_SHADERS "${OUTPUT_PATH}")
endforeach()

add_custom_target(compile_shaders ALL DEPENDS ${COMPILED_SHADERS})

list(LENGTH COMPILED_SHADERS SHADER_COUNT)
message(STATUS "Configured ${SHADER_COUNT} shaders for compilation")

# Export paths to parent scope for runtime library linking and shader lookup
set(SDL_shadercross_SOURCE_DIR ${SDL_shadercross_SOURCE_DIR} PARENT_SCOPE)
set(SHADER_OUTPUT_DIR "${SHADER_OUTPUT_DIR}" PARENT_SCOPE)
