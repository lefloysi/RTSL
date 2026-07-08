cmake_minimum_required(VERSION 3.20)

project(rtsl LANGUAGES C CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Rtsl)

if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET AND NOT VCPKG_TARGET_TRIPLET STREQUAL "")
    list(PREPEND CMAKE_PREFIX_PATH "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
endif()

# RTSL sources are grouped by pipeline layer under src/; includes are rooted
# at src/ (e.g. #include "frontend/lexer.hpp"). The public C ABI header is the
# only split-out surface and lives at bindings/c/include/rtsl.h.
#
#   support/   shared plumbing: types, source manager, diagnostics, binary io
#   frontend/  preprocessor, lexer, parser, AST (+ tokens/directives/builtins .def)
#   sema/      semantic analysis, mangling, uniform lowering, stage builtins
#   ir/        SSA RTIR: lowering, verification, disassembly (+ ops.def)
#   artifact/  artifact container serialization and the linker
#   driver/    compiler orchestration and the rtslc CLI
#   api/       the C API (rtsl.cpp) and the language service
#   runtime/   .rtslp reader for Rutile backends (not part of this library)

set(_rtsl_src "${CMAKE_CURRENT_SOURCE_DIR}/src")

set(RTSL_SOURCES
    ${_rtsl_src}/support/basic_diagnostics.cpp
    ${_rtsl_src}/support/basic_source_manager.cpp
    ${_rtsl_src}/support/io.cpp
    ${_rtsl_src}/frontend/preprocessor.cpp
    ${_rtsl_src}/frontend/lexer.cpp
    ${_rtsl_src}/frontend/parser.cpp
    ${_rtsl_src}/sema/sema.cpp
    ${_rtsl_src}/sema/stage_builtins.cpp
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
# `src/runtime/package.hpp/cpp` is a helper for reading .rtslp programs at runtime;
# it targets Rutile backends and depends on their `rutile.h`. Keeping it out
# of the RTSL library build inverts the dependency correctly — RTSL knows
# nothing about Rutile, and each backend compiles package.cpp itself when it
# needs the reader.

add_executable(rtslc ${_rtsl_src}/driver/rtslc.cpp)
find_package(CLI11 CONFIG QUIET)
if(CLI11_FOUND)
    target_link_libraries(rtslc PRIVATE rtsl CLI11::CLI11)
    target_compile_features(rtslc PRIVATE cxx_std_23)
    set_target_properties(rtslc PROPERTIES CXX_EXTENSIONS OFF)
else()
    message(WARNING "CLI11 not found; skipping rtslc target")
    set_target_properties(rtslc PROPERTIES EXCLUDE_FROM_ALL TRUE)
endif()

find_package(Catch2 CONFIG REQUIRED)

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
    set_target_properties(rtslc rtsl-tests PROPERTIES
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
    )
endif()

enable_testing()
add_test(NAME rtsl-tests COMMAND rtsl-tests)
