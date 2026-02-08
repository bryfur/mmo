# When using system SDL, find_package imported targets are directory-scoped
# so we need to find SDL3 again in this directory scope
if(NOT VENDORED_SDL)
    find_package(SDL3 REQUIRED QUIET)
endif()

# Dear ImGui (with SDL3 GPU backend) - only used by the editor
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.8
)
FetchContent_MakeAvailable(imgui)

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlgpu3.cpp
)
target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui PUBLIC
    $<IF:$<BOOL:${VENDORED_SDL}>,SDL3::SDL3-static,SDL3::SDL3>
)

file(GLOB_RECURSE EDITOR_SOURCES CONFIGURE_DEPENDS "*.cpp")
add_executable(mmo_editor ${EDITOR_SOURCES})

target_link_libraries(mmo_editor PRIVATE
    mmo_engine
    mmo_protocol
    mmo_server_lib
    imgui
    pthread
)

target_include_directories(mmo_editor PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

mmo_engine_copy_shaders(mmo_editor)
