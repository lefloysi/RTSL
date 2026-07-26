find_package(Catch2 CONFIG QUIET)

if(NOT Catch2_FOUND)
    message(STATUS "Catch2 not found; tests will not be built")
    return()
endif()

add_executable("rtsl-sdk-tests"
    "src/sdk.cpp"
)

target_compile_features("rtsl-sdk-tests" PRIVATE "cxx_std_23")

target_link_libraries("rtsl-sdk-tests" PRIVATE
    "rtsl-sdk"
    "Catch2::Catch2WithMain"
)

add_test(
    NAME "rtsl-sdk-tests"
    COMMAND "rtsl-sdk-tests"
)

add_executable("rtsl-tests"
    "src/basic.cpp"
    "src/compiler.cpp"
    "src/artifact.cpp"
    "src/hlsl.cpp"
    "src/parser_grammar.cpp"
    "src/parser_negative.cpp"
    "src/spirv.cpp"
    "src/glsl.cpp"
    "${PROJECT_SOURCE_DIR}/transpilers/hlsl/src/hlsl.cpp"
    "${PROJECT_SOURCE_DIR}/transpilers/glsl/src/glsl.cpp"
    "${PROJECT_SOURCE_DIR}/transpilers/spirv/src/spirv.cpp"
)

target_compile_features("rtsl-tests" PRIVATE "cxx_std_23")

target_compile_definitions("rtsl-tests" PRIVATE
    RTSL_TEST_SHADERS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/shaders"
)

target_include_directories("rtsl-tests" PRIVATE
    "${PROJECT_SOURCE_DIR}/rtsl/src"
    "${PROJECT_SOURCE_DIR}/rtsl-sdk/src"
    "${PROJECT_SOURCE_DIR}/rtsl-sdk/include"
    "${PROJECT_SOURCE_DIR}/transpilers/hlsl/include"
    "${PROJECT_SOURCE_DIR}/transpilers/glsl/include"
    "${PROJECT_SOURCE_DIR}/transpilers/spirv/include"
)

target_link_libraries("rtsl-tests" PRIVATE
    "rtsl"
    "Catch2::Catch2WithMain"
)

add_test(
    NAME "rtsl-tests"
    COMMAND "rtsl-tests"
)
