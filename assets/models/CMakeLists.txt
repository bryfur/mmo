file(GLOB_RECURSE MODEL_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.glb")

set(MODEL_BOUNDS_HEADER "${CMAKE_BINARY_DIR}/generated/model_bounds_generated.hpp")

add_custom_command(
    OUTPUT ${MODEL_BOUNDS_HEADER}
    COMMAND python3
        "${CMAKE_SOURCE_DIR}/tools/extract_model_bounds.py"
        --models-dir "${CMAKE_CURRENT_SOURCE_DIR}"
        --output "${MODEL_BOUNDS_HEADER}"
    DEPENDS ${MODEL_FILES} "${CMAKE_SOURCE_DIR}/tools/extract_model_bounds.py"
    COMMENT "Generating model bounds header from GLB files"
)

add_custom_target(generate_model_bounds DEPENDS ${MODEL_BOUNDS_HEADER})
