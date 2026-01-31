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

# Function for executables that link mmo_engine to copy shaders next to their binary
function(mmo_engine_copy_shaders target)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${MMO_SHADER_OUTPUT_DIR}"
            "$<TARGET_FILE_DIR:${target}>/shaders"
        COMMENT "Copying shaders to ${target} output directory"
    )
endfunction()
