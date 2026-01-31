# Fetch ASIO
FetchContent_Declare(
    asio
    GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
    GIT_TAG asio-1-30-2
)
FetchContent_MakeAvailable(asio)

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

# Fetch nlohmann/json for config loading
FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(json)

file(GLOB_RECURSE SERVER_SOURCES CONFIGURE_DEPENDS "*.cpp")
add_executable(mmo_server ${SERVER_SOURCES})

target_link_libraries(mmo_server PRIVATE
    mmo_common
    Jolt
    nlohmann_json::nlohmann_json
    pthread
)

target_include_directories(mmo_server PRIVATE
    ${asio_SOURCE_DIR}/asio/include
    ${JoltPhysics_SOURCE_DIR}/..
)
