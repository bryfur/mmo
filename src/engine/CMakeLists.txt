# Required SDL versions
set(SDL3_REQUIRED_VERSION "3.4.0")
set(SDL3_TTF_REQUIRED_VERSION "3.2.2")
set(SDL3_IMAGE_REQUIRED_VERSION "3.4.0")

# Try to find system SDL3 libraries at the required versions
find_package(SDL3 ${SDL3_REQUIRED_VERSION} QUIET)
find_package(SDL3_ttf ${SDL3_TTF_REQUIRED_VERSION} QUIET)
find_package(SDL3_image ${SDL3_IMAGE_REQUIRED_VERSION} QUIET)

if(SDL3_FOUND AND SDL3_ttf_FOUND AND SDL3_image_FOUND)
    set(VENDORED_SDL OFF CACHE INTERNAL "Whether SDL is vendored from source")
    message(STATUS "Using system SDL3 (${SDL3_VERSION}), SDL3_ttf (${SDL3_ttf_VERSION}), SDL3_image (${SDL3_image_VERSION})")
else()
    set(VENDORED_SDL ON CACHE INTERNAL "Whether SDL is vendored from source")
    message(STATUS "System SDL3 libs not found or wrong version, vendoring from source")

    # SDL3
    FetchContent_Declare(
        SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG release-${SDL3_REQUIRED_VERSION}
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
        GIT_TAG release-${SDL3_TTF_REQUIRED_VERSION}
    )
    set(SDLTTF_VENDORED ON CACHE BOOL "" FORCE)
    set(SDLTTF_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(SDL3_ttf)

    # SDL3_image
    FetchContent_Declare(
        SDL3_image
        GIT_REPOSITORY https://github.com/libsdl-org/SDL_image.git
        GIT_TAG release-${SDL3_IMAGE_REQUIRED_VERSION}
    )
    set(SDLIMAGE_VENDORED ON CACHE BOOL "" FORCE)
    set(SDLIMAGE_AVIF OFF CACHE BOOL "" FORCE)
    set(SDLIMAGE_DEPS_SHARED OFF CACHE BOOL "" FORCE)
    set(SDLIMAGE_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(SDL3_image)
endif()

# Fetch tinygltf (model loading)
FetchContent_Declare(
    tinygltf
    GIT_REPOSITORY https://github.com/syoyo/tinygltf.git
    GIT_TAG v2.9.7
)
set(TINYGLTF_HEADER_ONLY ON CACHE BOOL "" FORCE)
set(TINYGLTF_INSTALL OFF CACHE BOOL "" FORCE)
set(TINYGLTF_BUILD_LOADER_EXAMPLE OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(tinygltf)

# GLM (header-only math library)
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 1.0.3
)
set(GLM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glm)

# Compile shaders (HLSL -> SPIRV) at build time
# Also fetches SDL_shadercross for both CLI (build-time) and library (runtime)
add_subdirectory(shaders)

file(GLOB_RECURSE ENGINE_SOURCES CONFIGURE_DEPENDS "*.cpp")
add_library(mmo_engine STATIC ${ENGINE_SOURCES})

target_include_directories(mmo_engine PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${tinygltf_SOURCE_DIR}
    ${SDL_shadercross_SOURCE_DIR}/include
)

target_compile_definitions(mmo_engine PUBLIC GLM_FORCE_DEPTH_ZERO_TO_ONE)

target_link_libraries(mmo_engine PUBLIC
    $<IF:$<BOOL:${VENDORED_SDL}>,SDL3::SDL3-static,SDL3::SDL3>
    $<IF:$<BOOL:${VENDORED_SDL}>,SDL3_ttf::SDL3_ttf-static,SDL3_ttf::SDL3_ttf>
    $<IF:$<BOOL:${VENDORED_SDL}>,SDL3_image::SDL3_image-static,SDL3_image::SDL3_image>
    SDL3_shadercross::SDL3_shadercross-static
    glm::glm
)

# Ensure shaders are compiled before engine
add_dependencies(mmo_engine compile_shaders)

# Store shader output dir in cache so it's globally accessible
set(MMO_SHADER_OUTPUT_DIR "${SHADER_OUTPUT_DIR}" CACHE INTERNAL "Path to compiled shaders")

# Function for executables that link mmo_engine to copy shaders next to their binary.
# The copy_shaders target always runs after compile_shaders and copies the directory.
function(mmo_engine_copy_shaders target)
    add_custom_target(${target}_copy_shaders ALL
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${MMO_SHADER_OUTPUT_DIR}"
            "$<TARGET_FILE_DIR:${target}>/shaders"
        COMMENT "Copying shaders to ${target} output directory"
    )
    add_dependencies(${target}_copy_shaders compile_shaders)
    add_dependencies(${target} ${target}_copy_shaders)
endfunction()

