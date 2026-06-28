cmake_minimum_required(VERSION 3.20)

project(rtsl LANGUAGES C CXX)

# RTSL is split into sub-libraries so each piece can be reasoned about and
# linked independently. Source still lives under src/{Basic,Lex,Parse,...}
# in the existing clang-style layout; only the CMake targets are split.
#
# Dependency rules (enforced by the link lines below):
#
#   rtsl-frontend       -> (none)
#   rtsl-ir             -> rtsl-frontend       [transitively pulls AST/Sema;
#                                                refactor target: make IR pure]
#   rtsl-serialization  -> rtsl-ir
#   rtsl-linker         -> rtsl-serialization
#   rtsl-compiler       -> rtsl-frontend, rtsl-serialization
#   rtsl (aggregate)    -> compiler, linker, serialization, ir
#
# See ARCHITECTURE.md for the long-term split target (pure rtsl-ir, etc.).

set(_rtsl_src "${CMAKE_CURRENT_SOURCE_DIR}/src")

function(_rtsl_configure_lib target)
    target_compile_features(${target} PUBLIC cxx_std_23)
    set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF)
    target_include_directories(${target}
        PUBLIC
            "${_rtsl_src}"
            "${CMAKE_CURRENT_SOURCE_DIR}/bindings/c/include"
    )
endfunction()

# Foundation: Basic + AST + Lex + Parse + Sema.
add_library(rtsl-frontend STATIC
    ${_rtsl_src}/Basic/Diagnostics.cpp
    ${_rtsl_src}/Basic/SourceManager.cpp
    ${_rtsl_src}/Lex/Lexer.cpp
    ${_rtsl_src}/Parse/Parser.cpp
    ${_rtsl_src}/Sema/Sema.cpp
)
_rtsl_configure_lib(rtsl-frontend)

# IR data model + mangling. Today the IR includes AST and Sema headers so it
# transitively depends on rtsl-frontend; making it standalone is the main
# RTSL refactor item on the roadmap.
add_library(rtsl-ir STATIC
    ${_rtsl_src}/IR/IR.cpp
    ${_rtsl_src}/IR/UniformLowering.cpp
    ${_rtsl_src}/IR/StageBuiltins.cpp
    ${_rtsl_src}/Mangle/Mangler.cpp
)
_rtsl_configure_lib(rtsl-ir)
target_link_libraries(rtsl-ir PUBLIC rtsl-frontend)

# Artifact serialization: rtslo, rtsll, rtslm, rtslp, text RTIR.
add_library(rtsl-serialization STATIC
    ${_rtsl_src}/Serialization/Artifact.cpp
    ${_rtsl_src}/Serialization/TextRTIR.cpp
)
_rtsl_configure_lib(rtsl-serialization)
target_link_libraries(rtsl-serialization PUBLIC rtsl-ir)

# Linker: combines compiled objects/libraries into rtsll or rtslp.
add_library(rtsl-linker STATIC ${_rtsl_src}/Link/Linker.cpp)
_rtsl_configure_lib(rtsl-linker)
target_link_libraries(rtsl-linker PUBLIC rtsl-serialization)

# Compiler orchestrator: drives source -> rtslo / rtslm.
add_library(rtsl-compiler STATIC ${_rtsl_src}/Compiler/Compiler.cpp)
_rtsl_configure_lib(rtsl-compiler)
target_link_libraries(rtsl-compiler PUBLIC rtsl-frontend rtsl-serialization)

# Aggregate library that re-exports everything via the public rtsl.h API.
add_library(rtsl STATIC ${_rtsl_src}/rtsl.cpp)
_rtsl_configure_lib(rtsl)
target_link_libraries(rtsl
    PUBLIC
        rtsl-compiler
        rtsl-linker
        rtsl-serialization
        rtsl-ir
        rtsl-frontend
)

add_executable(rtslc ${_rtsl_src}/Driver/rtslc.cpp)
target_link_libraries(rtslc PRIVATE rtsl)
target_compile_features(rtslc PRIVATE cxx_std_23)
set_target_properties(rtslc PROPERTIES CXX_EXTENSIONS OFF)

add_executable(rtsl-tests tests/rtsl_tests.cpp)
target_link_libraries(rtsl-tests PRIVATE rtsl)
target_compile_features(rtsl-tests PRIVATE cxx_std_23)
set_target_properties(rtsl-tests PROPERTIES CXX_EXTENSIONS OFF)

enable_testing()
add_test(NAME rtsl-tests COMMAND rtsl-tests)

