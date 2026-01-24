cmake_minimum_required(VERSION 3.20)
project(mmo_game VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)

# Fetch EnTT
FetchContent_Declare(
    EnTT
    GIT_REPOSITORY https://github.com/skypjack/entt.git
    GIT_TAG v3.14.0
)
FetchContent_MakeAvailable(EnTT)

# Fetch JoltPhysics
FetchContent_Declare(
    JoltPhysics
    GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
    GIT_TAG v5.2.0
    SOURCE_SUBDIR Build
)
set(DOUBLE_PRECISION OFF CACHE BOOL "" FORCE)
set(GENERATE_DEBUG_SYMBOLS ON CACHE BOOL "" FORCE)
set(CROSS_PLATFORM_DETERMINISTIC OFF CACHE BOOL "" FORCE)
set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "" FORCE)
set(ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(ENABLE_ALL_WARNINGS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(JoltPhysics)

# Fetch tinygltf
FetchContent_Declare(
    tinygltf
    GIT_REPOSITORY https://github.com/syoyo/tinygltf.git
    GIT_TAG v2.9.7
)
set(TINYGLTF_HEADER_ONLY ON CACHE BOOL "" FORCE)
set(TINYGLTF_INSTALL OFF CACHE BOOL "" FORCE)
set(TINYGLTF_BUILD_LOADER_EXAMPLE OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(tinygltf)

# Fetch bgfx.cmake (CMake-friendly bgfx wrapper)
set(BGFX_BUILD_TOOLS ON CACHE BOOL "" FORCE)
set(BGFX_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BGFX_INSTALL OFF CACHE BOOL "" FORCE)
set(BGFX_CUSTOM_TARGETS OFF CACHE BOOL "" FORCE)
# Disable verbose debug logging in bgfx
set(BX_CONFIG_DEBUG OFF CACHE BOOL "" FORCE)
set(BGFX_CONFIG_DEBUG OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    bgfx_cmake
    GIT_REPOSITORY https://github.com/bkaradzic/bgfx.cmake.git
    GIT_TAG master
)
FetchContent_MakeAvailable(bgfx_cmake)

# Find required packages
find_package(asio CONFIG QUIET)
if(NOT asio_FOUND)
    find_path(ASIO_INCLUDE_DIR asio.hpp PATHS /usr/include /usr/local/include)
    if(NOT ASIO_INCLUDE_DIR)
        message(FATAL_ERROR "Could not find standalone asio. Install with: sudo apt install libasio-dev")
    endif()
endif()
find_package(SDL3 REQUIRED)
find_package(SDL3_ttf REQUIRED)
find_package(SDL3_image REQUIRED)
find_package(glm REQUIRED)

# Common library (shared between server and client)
add_library(mmo_common STATIC
    common/protocol.cpp
    common/ecs/components.cpp
)
target_include_directories(mmo_common PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${JoltPhysics_SOURCE_DIR}/..
)
target_link_libraries(mmo_common PUBLIC EnTT::EnTT Jolt)

# Server executable
add_executable(mmo_server
    server/main.cpp
    server/server.cpp
    server/session.cpp
    server/world.cpp
    server/systems/movement_system.cpp
    server/systems/combat_system.cpp
    server/systems/ai_system.cpp
    server/systems/physics_system.cpp
)
target_link_libraries(mmo_server PRIVATE 
    mmo_common 
    pthread
)
if(ASIO_INCLUDE_DIR)
    target_include_directories(mmo_server PRIVATE ${ASIO_INCLUDE_DIR})
endif()

# Shader compilation function for bgfx
function(compile_bgfx_shaders TARGET_NAME SHADER_DIR OUTPUT_DIR)
    file(GLOB_RECURSE VERTEX_SHADERS "${SHADER_DIR}/*_vs.sc")
    file(GLOB_RECURSE FRAGMENT_SHADERS "${SHADER_DIR}/*_fs.sc")
    file(GLOB_RECURSE COMPUTE_SHADERS "${SHADER_DIR}/*_cs.sc")
    
    set(COMPILED_SHADERS "")
    
    # Determine shader model based on platform
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set(SHADER_PLATFORM "linux")
        set(VS_PROFILE "spirv")
        set(FS_PROFILE "spirv")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        set(SHADER_PLATFORM "windows")
        set(VS_PROFILE "s_5_0")
        set(FS_PROFILE "s_5_0")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        set(SHADER_PLATFORM "osx")
        set(VS_PROFILE "metal")
        set(FS_PROFILE "metal")
    endif()
    
    foreach(SHADER_FILE ${VERTEX_SHADERS})
        get_filename_component(SHADER_NAME ${SHADER_FILE} NAME_WE)
        # Get the base name (remove _vs suffix) and look for varying file
        string(REGEX REPLACE "_vs$" "" SHADER_BASE ${SHADER_NAME})
        set(VARYING_FILE "${SHADER_DIR}/${SHADER_BASE}_varying.def.sc")
        if(NOT EXISTS ${VARYING_FILE})
            set(VARYING_FILE "${SHADER_DIR}/varying.def.sc")
        endif()
        set(OUTPUT_FILE "${OUTPUT_DIR}/${SHADER_NAME}.bin")
        add_custom_command(
            OUTPUT ${OUTPUT_FILE}
            COMMAND $<TARGET_FILE:shaderc> 
                -f ${SHADER_FILE}
                -o ${OUTPUT_FILE}
                --type vertex
                --platform ${SHADER_PLATFORM}
                -p ${VS_PROFILE}
                -i ${bgfx_cmake_SOURCE_DIR}/bgfx/src
                -i ${SHADER_DIR}
                --varyingdef ${VARYING_FILE}
            DEPENDS ${SHADER_FILE} ${VARYING_FILE} shaderc
            COMMENT "Compiling vertex shader ${SHADER_NAME}"
        )
        list(APPEND COMPILED_SHADERS ${OUTPUT_FILE})
    endforeach()
    
    foreach(SHADER_FILE ${FRAGMENT_SHADERS})
        get_filename_component(SHADER_NAME ${SHADER_FILE} NAME_WE)
        # Get the base name (remove _fs suffix) and look for varying file
        string(REGEX REPLACE "_fs$" "" SHADER_BASE ${SHADER_NAME})
        set(VARYING_FILE "${SHADER_DIR}/${SHADER_BASE}_varying.def.sc")
        if(NOT EXISTS ${VARYING_FILE})
            set(VARYING_FILE "${SHADER_DIR}/varying.def.sc")
        endif()
        set(OUTPUT_FILE "${OUTPUT_DIR}/${SHADER_NAME}.bin")
        add_custom_command(
            OUTPUT ${OUTPUT_FILE}
            COMMAND $<TARGET_FILE:shaderc>
                -f ${SHADER_FILE}
                -o ${OUTPUT_FILE}
                --type fragment
                --platform ${SHADER_PLATFORM}
                -p ${FS_PROFILE}
                -i ${bgfx_cmake_SOURCE_DIR}/bgfx/src
                -i ${SHADER_DIR}
                --varyingdef ${VARYING_FILE}
            DEPENDS ${SHADER_FILE} ${VARYING_FILE} shaderc
            COMMENT "Compiling fragment shader ${SHADER_NAME}"
        )
        list(APPEND COMPILED_SHADERS ${OUTPUT_FILE})
    endforeach()

    foreach(SHADER_FILE ${COMPUTE_SHADERS})
        get_filename_component(SHADER_NAME ${SHADER_FILE} NAME_WE)
        set(OUTPUT_FILE "${OUTPUT_DIR}/${SHADER_NAME}.bin")
        add_custom_command(
            OUTPUT ${OUTPUT_FILE}
            COMMAND $<TARGET_FILE:shaderc>
                -f ${SHADER_FILE}
                -o ${OUTPUT_FILE}
                --type compute
                --platform ${SHADER_PLATFORM}
                -p ${VS_PROFILE}
                -i ${bgfx_cmake_SOURCE_DIR}/bgfx/src
                -i ${SHADER_DIR}
            DEPENDS ${SHADER_FILE} shaderc
            COMMENT "Compiling compute shader ${SHADER_NAME}"
        )
        list(APPEND COMPILED_SHADERS ${OUTPUT_FILE})
    endforeach()
    
    add_custom_target(${TARGET_NAME}_shaders DEPENDS ${COMPILED_SHADERS})
    add_dependencies(${TARGET_NAME} ${TARGET_NAME}_shaders)
endfunction()

# Client executable
add_executable(mmo_client
    client/main.cpp
    client/game.cpp
    client/network_client.cpp
    client/renderer.cpp
    client/input_handler.cpp
    client/model_loader.cpp
    # Systems
    client/systems/interpolation_system.cpp
    client/systems/camera_system.cpp
    # Render subsystems
    client/render/render_context.cpp
    client/render/terrain_renderer.cpp
    client/render/world_renderer.cpp
    client/render/ui_renderer.cpp
    client/render/effect_renderer.cpp
    client/render/shadow_system.cpp
    client/render/grass_renderer.cpp
    client/render/text_renderer.cpp
    client/render/bgfx_utils.cpp
)

target_link_libraries(mmo_client PRIVATE 
    mmo_common 
    SDL3::SDL3
    SDL3_ttf::SDL3_ttf
    SDL3_image::SDL3_image
    bgfx
    bx
    bimg
    glm::glm
    pthread
    dl
)

target_include_directories(mmo_client PRIVATE 
    ${tinygltf_SOURCE_DIR}
)

if(ASIO_INCLUDE_DIR)
    target_include_directories(mmo_client PRIVATE ${ASIO_INCLUDE_DIR})
endif()

# Create shader output directory
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/shaders)

# Compile shaders
compile_bgfx_shaders(mmo_client ${CMAKE_SOURCE_DIR}/client/shaders ${CMAKE_BINARY_DIR}/shaders)
