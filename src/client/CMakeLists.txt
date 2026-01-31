# Fetch ASIO (client-only dependency for networking)
FetchContent_Declare(
    asio
    GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
    GIT_TAG asio-1-30-2
)
FetchContent_MakeAvailable(asio)

file(GLOB_RECURSE CLIENT_SOURCES CONFIGURE_DEPENDS "*.cpp")
add_executable(mmo_client ${CLIENT_SOURCES})

target_link_libraries(mmo_client PRIVATE
    mmo_engine
    mmo_common
    pthread
)

target_include_directories(mmo_client PRIVATE ${asio_SOURCE_DIR}/asio/include)

mmo_engine_copy_shaders(mmo_client)
