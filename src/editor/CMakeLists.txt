file(GLOB_RECURSE EDITOR_SOURCES CONFIGURE_DEPENDS "*.cpp")
add_executable(mmo_editor ${EDITOR_SOURCES})

target_link_libraries(mmo_editor PRIVATE
    mmo_engine
    mmo_protocol
    mmo_server_lib
    pthread
)

target_include_directories(mmo_editor PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

mmo_engine_copy_shaders(mmo_editor)
