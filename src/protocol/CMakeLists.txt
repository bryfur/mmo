file(GLOB_RECURSE COMMON_SOURCES CONFIGURE_DEPENDS "*.cpp")
add_library(mmo_protocol STATIC ${COMMON_SOURCES})

target_include_directories(mmo_protocol PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/generated
)

target_link_libraries(mmo_protocol PUBLIC EnTT::EnTT)
add_dependencies(mmo_protocol generate_model_bounds)
