cmake_minimum_required(VERSION 3.20)

project(rtsl LANGUAGES C CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Rtsl)

if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET AND NOT VCPKG_TARGET_TRIPLET STREQUAL "")
    list(PREPEND CMAKE_PREFIX_PATH "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
endif()

# RTSL is split into sibling project folders:
#
#   rtsl/       compiler library implementation
#   rtslc/      command line tool
#   rtsl-sdk/   shared SDK code for compiler and backend/transpiler code

set(_rtsl_src "${CMAKE_CURRENT_SOURCE_DIR}/rtsl")
set(_rtslc_src "${CMAKE_CURRENT_SOURCE_DIR}/rtslc")
set(_rtsl_sdk_include "${CMAKE_CURRENT_SOURCE_DIR}/rtsl-sdk/include")

set(RTSL_SOURCES
    ${_rtsl_src}/support/basic_diagnostics.cpp
    ${_rtsl_src}/support/basic_source_manager.cpp
    ${_rtsl_src}/support/io.cpp
    ${_rtsl_src}/frontend/preprocessor.cpp
    ${_rtsl_src}/frontend/lexer.cpp
    ${_rtsl_src}/frontend/parser.cpp
    ${_rtsl_src}/sema/sema.cpp
    ${_rtsl_src}/sema/mangler.cpp
    ${_rtsl_src}/sema/uniform_lowering.cpp
    ${_rtsl_src}/ir/ir.cpp
    ${_rtsl_src}/ir/text_rtir.cpp
    ${_rtsl_src}/artifact/artifact.cpp
    ${_rtsl_src}/artifact/linker.cpp
    ${_rtsl_src}/driver/compiler.cpp
    ${_rtsl_src}/api/language_service.cpp
    ${_rtsl_src}/api/rtsl.cpp
)

add_library(rtsl STATIC ${RTSL_SOURCES})
target_compile_features(rtsl PUBLIC cxx_std_23)
set_target_properties(rtsl PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
    set_target_properties(rtsl PROPERTIES
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
    )
    target_compile_options(rtsl PRIVATE /FS)
endif()
target_include_directories(rtsl
    PUBLIC
        "${_rtsl_src}"
        "${CMAKE_CURRENT_SOURCE_DIR}/bindings/c/include"
)

add_library(rtsl-sdk STATIC
    "${CMAKE_CURRENT_SOURCE_DIR}/rtsl-sdk/src/artifact.cpp"
)
target_compile_features(rtsl-sdk PUBLIC cxx_std_23)
set_target_properties(rtsl-sdk PROPERTIES CXX_EXTENSIONS OFF)
target_include_directories(rtsl-sdk
    PUBLIC
        "${_rtsl_sdk_include}"
)
if(MSVC)
    set_target_properties(rtsl-sdk PROPERTIES
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
    )
    target_compile_options(rtsl-sdk PRIVATE /FS)
endif()
target_link_libraries(rtsl PUBLIC rtsl-sdk)

add_executable(rtslc ${_rtslc_src}/rtslc.cpp)
find_package(CLI11 CONFIG QUIET)
if(CLI11_FOUND)
    target_link_libraries(rtslc PRIVATE rtsl CLI11::CLI11)
    target_compile_features(rtslc PRIVATE cxx_std_23)
    set_target_properties(rtslc PROPERTIES CXX_EXTENSIONS OFF)
else()
    message(WARNING "CLI11 not found; skipping rtslc target")
    set_target_properties(rtslc PROPERTIES EXCLUDE_FROM_ALL TRUE)
endif()

enable_testing()
find_package(Catch2 CONFIG QUIET)
if(Catch2_FOUND)
    add_executable(rtsl-tests
        tests/basic.cpp
        tests/compiler.cpp
        tests/artifact.cpp
        tests/parser_grammar.cpp
        tests/parser_negative.cpp
    )
    target_link_libraries(rtsl-tests PRIVATE rtsl)
    target_link_libraries(rtsl-tests PRIVATE Catch2::Catch2WithMain)
    target_compile_features(rtsl-tests PRIVATE cxx_std_23)
    set_target_properties(rtsl-tests PROPERTIES CXX_EXTENSIONS OFF)
    # Smoke tests iterate tests/shaders/*.rtsl; the path is baked in at build
    # time so the test binary works no matter where CTest launches it from.
    target_compile_definitions(rtsl-tests PRIVATE
        RTSL_TEST_SHADERS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/shaders"
    )

    if(MSVC)
        set_target_properties(rtsl-tests PROPERTIES
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
        )
    endif()

    add_test(NAME rtsl-tests COMMAND rtsl-tests)
else()
    message(WARNING "Catch2 not found; skipping rtsl-tests target")
endif()

if(TARGET rtslc)
    set(_rtsl_smoke_dir "${CMAKE_CURRENT_BINARY_DIR}/smoke")
    add_test(NAME rtslc-smoke-prepare
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${_rtsl_smoke_dir}"
    )
    add_test(NAME rtslc-smoke-compile
        COMMAND rtslc compile
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/shaders/graphics_program.rtsl"
            -o "${_rtsl_smoke_dir}/graphics_program.rtslo"
    )
    add_test(NAME rtslc-smoke-link
        COMMAND rtslc link-program
            "${_rtsl_smoke_dir}/graphics_program.rtslo"
            -o "${_rtsl_smoke_dir}/graphics_program.rtslp"
    )
    add_test(NAME rtslc-smoke-dump
        COMMAND rtslc dump "${_rtsl_smoke_dir}/graphics_program.rtslp"
    )
    set_tests_properties(rtslc-smoke-prepare PROPERTIES FIXTURES_SETUP rtslc_smoke)
    set_tests_properties(rtslc-smoke-compile PROPERTIES FIXTURES_REQUIRED rtslc_smoke FIXTURES_SETUP rtslc_smoke_object)
    set_tests_properties(rtslc-smoke-link PROPERTIES FIXTURES_REQUIRED rtslc_smoke_object FIXTURES_SETUP rtslc_smoke_program)
    set_tests_properties(rtslc-smoke-dump PROPERTIES FIXTURES_REQUIRED rtslc_smoke_program)
endif()

if(MSVC)
    set_target_properties(rtslc PROPERTIES
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
    )
endif()
