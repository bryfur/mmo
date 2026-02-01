add_library(mmo_protocol INTERFACE)

target_include_directories(mmo_protocol INTERFACE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/generated
)

target_link_libraries(mmo_protocol INTERFACE EnTT::EnTT)
add_dependencies(mmo_protocol generate_model_bounds)
