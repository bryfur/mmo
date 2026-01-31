# Fetch EnTT
FetchContent_Declare(
    EnTT
    GIT_REPOSITORY https://github.com/skypjack/entt.git
    GIT_TAG v3.14.0
)
FetchContent_MakeAvailable(EnTT)

file(GLOB_RECURSE COMMON_SOURCES CONFIGURE_DEPENDS "*.cpp")
add_library(mmo_common STATIC ${COMMON_SOURCES})

target_include_directories(mmo_common PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/generated
)

target_link_libraries(mmo_common PUBLIC EnTT::EnTT)
add_dependencies(mmo_common generate_model_bounds)
