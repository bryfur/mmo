cmake_minimum_required(VERSION 3.20)
project(mmo_game VERSION 0.1.0 LANGUAGES CXX)

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

# Fetch ASIO
FetchContent_Declare(
    asio
    GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
    GIT_TAG asio-1-30-2
)
FetchContent_MakeAvailable(asio)

# Fetch SDL_shadercross for runtime HLSL compilation
# Uses VENDORED mode to build SPIRV-Cross from SDL_shadercross's external/ submodules
# NOTE: Pinned to specific commit for reproducible builds
FetchContent_Declare(
    SDL_shadercross
    GIT_REPOSITORY https://github.com/libsdl-org/SDL_shadercross.git
    GIT_TAG 7b7365a86611b2a7b6462e521cf1c43a037d0970
    GIT_SUBMODULES_RECURSE TRUE
)
set(SDLSHADERCROSS_CLI OFF CACHE BOOL "" FORCE)
set(SDLSHADERCROSS_SHARED OFF CACHE BOOL "" FORCE)
set(SDLSHADERCROSS_STATIC ON CACHE BOOL "" FORCE)
set(SDLSHADERCROSS_INSTALL OFF CACHE BOOL "" FORCE)
set(SDLSHADERCROSS_VENDORED ON CACHE BOOL "" FORCE)
set(SDLSHADERCROSS_SPIRVCROSS_SHARED ON CACHE BOOL "" FORCE)
set(SDLSHADERCROSS_DXC OFF CACHE BOOL "" FORCE)
set(CMAKE_POSITION_INDEPENDENT_CODE ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(SDL_shadercross)

# Find required packages
find_package(SDL3 REQUIRED)
find_package(SDL3_ttf REQUIRED)
find_package(SDL3_image REQUIRED)
find_package(OpenGL REQUIRED)
find_package(GLEW REQUIRED)
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
target_include_directories(mmo_server PRIVATE ${asio_SOURCE_DIR}/asio/include)

# Client executable
add_executable(mmo_client
    client/main.cpp
    client/game.cpp
    client/network_client.cpp
    client/renderer.cpp
    client/shader.cpp
    client/input_handler.cpp
    client/model_loader.cpp
    # Systems
    client/systems/interpolation_system.cpp
    client/systems/camera_system.cpp
    client/systems/render_system.cpp
    # Scene
    client/scene/render_scene.cpp
    client/scene/ui_scene.cpp
    # Render subsystems
    client/render/render_context.cpp
    client/render/terrain_renderer.cpp
    client/render/world_renderer.cpp
    client/render/ui_renderer.cpp
    client/render/effect_renderer.cpp
    client/render/shadow_system.cpp
    client/render/grass_renderer.cpp
    client/render/text_renderer.cpp
    # GPU abstraction layer (SDL3 GPU API)
    client/gpu/gpu_device.cpp
    client/gpu/gpu_buffer.cpp
    client/gpu/gpu_texture.cpp
    client/gpu/gpu_pipeline.cpp
    client/gpu/gpu_shader.cpp
    client/gpu/pipeline_registry.cpp
)
target_link_libraries(mmo_client PRIVATE 
    mmo_common 
    SDL3::SDL3
    SDL3_ttf::SDL3_ttf
    SDL3_image::SDL3_image
    SDL3_shadercross::SDL3_shadercross-static
    OpenGL::GL
    GLEW::GLEW
    glm::glm
    pthread
)
target_include_directories(mmo_client PRIVATE ${tinygltf_SOURCE_DIR})
target_include_directories(mmo_client PRIVATE ${asio_SOURCE_DIR}/asio/include)
target_include_directories(mmo_client PRIVATE ${SDL_shadercross_SOURCE_DIR}/include)
