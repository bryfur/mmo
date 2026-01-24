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
if(ASIO_INCLUDE_DIR)
    target_include_directories(mmo_server PRIVATE ${ASIO_INCLUDE_DIR})
endif()

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
    # Render subsystems
    client/render/render_context.cpp
    client/render/terrain_renderer.cpp
    client/render/world_renderer.cpp
    client/render/ui_renderer.cpp
    client/render/effect_renderer.cpp
    client/render/shadow_system.cpp
    client/render/grass_renderer.cpp
    client/render/text_renderer.cpp
)
target_link_libraries(mmo_client PRIVATE 
    mmo_common 
    SDL3::SDL3
    SDL3_ttf::SDL3_ttf
    SDL3_image::SDL3_image
    OpenGL::GL
    GLEW::GLEW
    glm::glm
    pthread
)
target_include_directories(mmo_client PRIVATE ${tinygltf_SOURCE_DIR})
if(ASIO_INCLUDE_DIR)
    target_include_directories(mmo_client PRIVATE ${ASIO_INCLUDE_DIR})
endif()
