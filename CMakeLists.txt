cmake_minimum_required(VERSION 3.28)
project(RTSL VERSION 0.1.0 LANGUAGES CXX)

add_library(rtsl STATIC
	src/AST/ASTContext.cpp
	src/Basic/Diagnostic.cpp
	src/Basic/IdentifierTable.cpp
	src/Basic/SourceManager.cpp
	src/Basic/TokenKinds.cpp
	src/CodeGen/CodeGenerator.cpp
	src/Frontend/CompilerInstance.cpp
	src/Frontend/ModuleInterface.cpp
	src/Frontend/ModuleInterfaceBuilder.cpp
	src/Frontend/ModuleInterfaceImporter.cpp
	src/IR/Builder.cpp
	src/IR/IR.cpp
	src/IR/Verifier.cpp
	src/Lex/Lexer.cpp
	src/Lex/Preprocessor.cpp
	src/Linker/Linker.cpp
	src/Parse/Parser.cpp
	src/Serialization/Artifact.cpp
	src/Serialization/DebugArtifact.cpp
	src/Sema/Sema.cpp
)
add_library(RTSL::rtsl ALIAS rtsl)

target_compile_features(rtsl PUBLIC cxx_std_20)
target_compile_options(rtsl PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/EHsc>)
target_include_directories(rtsl PUBLIC
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
	$<INSTALL_INTERFACE:include>
)
target_include_directories(rtsl PRIVATE src)

option(RTSL_BUILD_CLI "Build the rtslc command-line driver" ${PROJECT_IS_TOP_LEVEL})
if(RTSL_BUILD_CLI)
	add_subdirectory(tools/rtslc)
endif()
add_subdirectory(tools/rtsl-lsp)

option(RTSL_BUILD_TESTS "Build RTSL tests" ${PROJECT_IS_TOP_LEVEL})
if(RTSL_BUILD_TESTS)
	include(CTest)
	add_subdirectory(tests)
endif()

