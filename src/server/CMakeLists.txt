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
file(GLOB_RECURSE SERVER_TEST_SOURCES CONFIGURE_DEPENDS "*.test.cpp")

# Separate main.cpp + test sources from the library sources
list(REMOVE_ITEM SERVER_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/main.cpp")
foreach(test_src ${SERVER_TEST_SOURCES})
    list(REMOVE_ITEM SERVER_SOURCES ${test_src})
endforeach()

# Create server library (for use by editor and other tools)
add_library(mmo_server_lib ${SERVER_SOURCES})

target_link_libraries(mmo_server_lib PUBLIC
    mmo_protocol
    Jolt
    nlohmann_json::nlohmann_json
    sqlite3
    pthread
)

target_include_directories(mmo_server_lib PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${asio_SOURCE_DIR}/asio/include
    ${JoltPhysics_SOURCE_DIR}/..
)

# Create server executable
add_executable(mmo_server main.cpp)

target_link_libraries(mmo_server PRIVATE
    mmo_server_lib
)

target_include_directories(mmo_server PRIVATE
    ${asio_SOURCE_DIR}/asio/include
)

# Server unit tests
if(MMO_BUILD_TESTS AND SERVER_TEST_SOURCES)
    add_executable(mmo_server_tests ${SERVER_TEST_SOURCES})
    target_link_libraries(mmo_server_tests PRIVATE
        mmo_server_lib
        GTest::gtest_main
    )
    target_include_directories(mmo_server_tests PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${asio_SOURCE_DIR}/asio/include
    )
    include(GoogleTest)
    gtest_discover_tests(mmo_server_tests)
endif()
