file(GLOB_RECURSE CLIENT_SOURCES CONFIGURE_DEPENDS "*.cpp")
add_executable(mmo_client ${CLIENT_SOURCES})

target_link_libraries(mmo_client PRIVATE
    mmo_engine
    mmo_protocol
    pthread
)

target_include_directories(mmo_client PRIVATE ${asio_SOURCE_DIR}/asio/include)

mmo_engine_copy_shaders(mmo_client)
