cmake_minimum_required(VERSION 3.28)
project(RTSL VERSION 0.1.0 LANGUAGES CXX)

# Graphics backends still consume the stable C++ IR/artifact reader.
# The compiler, tools and direct tests are built by Kiln from Rutile sources.
add_library(rtsl STATIC
 src/IR/IR.cpp
 src/IR/Verifier.cpp
 src/Serialization/Artifact.cpp
)
set_target_properties(rtsl PROPERTIES POSITION_INDEPENDENT_CODE ON)
add_library(RTSL::rtsl ALIAS rtsl)
target_compile_features(rtsl PUBLIC cxx_std_20)
target_compile_options(rtsl PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/EHsc>)
target_include_directories(rtsl PUBLIC
 $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
 $<INSTALL_INTERFACE:include>
)

# CMake consumers may use a compiler already built with Kiln.
set(RTSLC_EXECUTABLE "" CACHE FILEPATH "Kiln-built Rutile RTSL compiler executable")
if(RTSLC_EXECUTABLE)
 add_executable(rtslc IMPORTED GLOBAL)
 set_target_properties(rtslc PROPERTIES IMPORTED_LOCATION "${RTSLC_EXECUTABLE}")
endif()
