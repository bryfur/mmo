# Compile shaders (HLSL -> SPIRV) at build time
# Also fetches SDL_shadercross for both CLI (build-time) and library (runtime)
add_subdirectory(shaders)

# Static linking for all vendored dependencies
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

# Fetch tinygltf (client-only dependency)
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

# Static linking for all vendored dependencies
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

# Find or fetch SDL3 libraries
if(VENDORED_SDL)
    # SDL3
    FetchContent_Declare(
        SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG release-3.4.0
    )
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_X11_XTEST OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(SDL3)

    # SDL3_ttf
    FetchContent_Declare(
        SDL3_ttf
        GIT_REPOSITORY https://github.com/libsdl-org/SDL_ttf.git
        GIT_TAG release-3.2.2
    )
    set(SDLTTF_VENDORED ON CACHE BOOL "" FORCE)
    set(SDLTTF_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(SDL3_ttf)

    # SDL3_image
    FetchContent_Declare(
        SDL3_image
        GIT_REPOSITORY https://github.com/libsdl-org/SDL_image.git
        GIT_TAG release-3.4.0
        
    )
    set(SDLIMAGE_VENDORED ON CACHE BOOL "" FORCE)
    set(SDLIMAGE_AVIF OFF CACHE BOOL "" FORCE)
    set(SDLIMAGE_DEPS_SHARED OFF CACHE BOOL "" FORCE)
    set(SDLIMAGE_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(SDL3_image)
else()
    find_package(SDL3 REQUIRED)
    find_package(SDL3_ttf REQUIRED)
    find_package(SDL3_image REQUIRED)
endif()

# GLM (header-only math library)
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 1.0.3
)
set(GLM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glm)

add_executable(mmo_client
    main.cpp
    game.cpp
    network_client.cpp
    renderer.cpp
    input_handler.cpp
    model_loader.cpp
    # Systems
    systems/interpolation_system.cpp
    systems/camera_system.cpp
    systems/render_system.cpp
    # Scene
    scene/render_scene.cpp
    scene/ui_scene.cpp
    # Render subsystems
    render/render_context.cpp
    render/terrain_renderer.cpp
    render/world_renderer.cpp
    render/ui_renderer.cpp
    render/effect_renderer.cpp
    render/grass_renderer.cpp
    render/text_renderer.cpp
    # GPU abstraction layer (SDL3 GPU API)
    gpu/gpu_device.cpp
    gpu/gpu_buffer.cpp
    gpu/gpu_texture.cpp
    gpu/gpu_pipeline.cpp
    gpu/gpu_shader.cpp
    gpu/pipeline_registry.cpp
)

target_link_libraries(mmo_client PRIVATE
    mmo_common
    $<IF:$<BOOL:${VENDORED_SDL}>,SDL3::SDL3-static,SDL3::SDL3>
    $<IF:$<BOOL:${VENDORED_SDL}>,SDL3_ttf::SDL3_ttf-static,SDL3_ttf::SDL3_ttf>
    $<IF:$<BOOL:${VENDORED_SDL}>,SDL3_image::SDL3_image-static,SDL3_image::SDL3_image>
    SDL3_shadercross::SDL3_shadercross-static
    glm::glm
    pthread
)

target_compile_definitions(mmo_client PRIVATE GLM_FORCE_DEPTH_ZERO_TO_ONE)
target_include_directories(mmo_client PRIVATE ${tinygltf_SOURCE_DIR})
target_include_directories(mmo_client PRIVATE ${asio_SOURCE_DIR}/asio/include)
target_include_directories(mmo_client PRIVATE ${SDL_shadercross_SOURCE_DIR}/include)

# Ensure shaders are compiled before client
add_dependencies(mmo_client compile_shaders)
