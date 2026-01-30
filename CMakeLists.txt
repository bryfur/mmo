cmake_minimum_required(VERSION 3.20)
project(mmo_game VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)

# Option to build SDL3 and satellite libraries from source (used by client)
option(VENDORED_SDL "Build SDL3, SDL3_ttf, SDL3_image from source via FetchContent" ON)

# Generate model bounds header from GLB files
add_subdirectory(assets/models)

add_subdirectory(src/common)
add_subdirectory(src/server)
add_subdirectory(src/client)
