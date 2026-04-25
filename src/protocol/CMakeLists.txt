add_library(mmo_protocol INTERFACE)

target_include_directories(mmo_protocol INTERFACE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/generated
)

target_link_libraries(mmo_protocol INTERFACE EnTT::EnTT)
add_dependencies(mmo_protocol generate_model_bounds)

# Protocol unit tests (pure wire-format round-trips)
if(MMO_BUILD_TESTS)
    file(GLOB_RECURSE PROTOCOL_TEST_SOURCES CONFIGURE_DEPENDS "*.test.cpp")
    if(PROTOCOL_TEST_SOURCES)
        add_executable(mmo_protocol_tests ${PROTOCOL_TEST_SOURCES})
        target_link_libraries(mmo_protocol_tests PRIVATE mmo_protocol GTest::gtest_main)
        target_include_directories(mmo_protocol_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
        include(GoogleTest)
        gtest_discover_tests(mmo_protocol_tests)
    endif()
endif()
