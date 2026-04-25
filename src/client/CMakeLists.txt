file(GLOB_RECURSE CLIENT_SOURCES CONFIGURE_DEPENDS "*.cpp")
# Exclude test files and main.cpp from the client library so tests can link
# against client code without dragging in the entry point.
list(FILTER CLIENT_SOURCES EXCLUDE REGEX ".*\\.test\\.cpp$")
list(FILTER CLIENT_SOURCES EXCLUDE REGEX ".*/main\\.cpp$")

add_library(mmo_client_lib STATIC ${CLIENT_SOURCES})
target_link_libraries(mmo_client_lib PUBLIC
    mmo_engine
    mmo_protocol
    nlohmann_json::nlohmann_json
    pthread
)
target_include_directories(mmo_client_lib PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${asio_SOURCE_DIR}/asio/include
)

add_executable(mmo_client main.cpp)
target_link_libraries(mmo_client PRIVATE mmo_client_lib)

mmo_engine_copy_shaders(mmo_client)

# Unit tests
if(MMO_BUILD_TESTS)
    file(GLOB_RECURSE CLIENT_TEST_SOURCES CONFIGURE_DEPENDS "*.test.cpp")
    if(CLIENT_TEST_SOURCES)
        add_executable(mmo_client_tests ${CLIENT_TEST_SOURCES})
        target_link_libraries(mmo_client_tests PRIVATE mmo_client_lib GTest::gtest_main)

        include(GoogleTest)
        gtest_discover_tests(mmo_client_tests)
    endif()
endif()
