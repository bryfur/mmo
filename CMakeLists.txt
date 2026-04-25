cmake_minimum_required(VERSION 3.20)
project(mmo_game VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)

# Static linking for all vendored dependencies
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

# Tracy profiler (opt-in; OFF means zero overhead)
option(MMO_ENABLE_TRACY "Enable Tracy profiler instrumentation" OFF)
if(MMO_ENABLE_TRACY)
    FetchContent_Declare(
        tracy
        GIT_REPOSITORY https://github.com/wolfpld/tracy.git
        GIT_TAG v0.11.1
    )
    set(TRACY_ENABLE ON CACHE BOOL "" FORCE)
    set(TRACY_ON_DEMAND ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(tracy)
endif()

# EnTT (ECS library)
FetchContent_Declare(
    EnTT
    GIT_REPOSITORY https://github.com/skypjack/entt.git
    GIT_TAG v3.14.0
)
FetchContent_MakeAvailable(EnTT)

# ASIO (networking library)
FetchContent_Declare(
    asio
    GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
    GIT_TAG asio-1-30-2
)
FetchContent_MakeAvailable(asio)

# nlohmann/json (JSON parsing library)
FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(json)

# SQLite amalgamation (server-side persistence). The amalgamation is a single
# .c/.h pair we build into a static lib; no external dependency on system libsqlite3.
FetchContent_Declare(
    sqlite_amalgamation
    URL https://www.sqlite.org/2024/sqlite-amalgamation-3450200.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(sqlite_amalgamation)
add_library(sqlite3 STATIC ${sqlite_amalgamation_SOURCE_DIR}/sqlite3.c)
target_include_directories(sqlite3 PUBLIC ${sqlite_amalgamation_SOURCE_DIR})
target_compile_definitions(sqlite3 PUBLIC
    SQLITE_THREADSAFE=1
    SQLITE_ENABLE_FTS5=0
    SQLITE_OMIT_DEPRECATED=1
    SQLITE_OMIT_LOAD_EXTENSION=1
)
if(NOT WIN32)
    target_link_libraries(sqlite3 PUBLIC ${CMAKE_DL_LIBS} pthread)
endif()
# SQLite amalgamation generates many warnings under -Wall; suppress on this target.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(sqlite3 PRIVATE -w)
endif()

# Unit tests
option(MMO_BUILD_TESTS "Build unit tests" ON)
if(MMO_BUILD_TESTS)
    enable_testing()

    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.15.2
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
endif()


# Generate model bounds header from GLB files
add_subdirectory(assets/models)

add_subdirectory(src/protocol)
add_subdirectory(src/engine)
add_subdirectory(src/server)
add_subdirectory(src/client)
add_subdirectory(src/editor)

