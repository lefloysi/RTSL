cmake_minimum_required(VERSION 3.20)

project(rtsl LANGUAGES C CXX)

add_library(rtsl STATIC)

target_compile_features(rtsl PUBLIC cxx_std_17)

target_include_directories(rtsl
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_sources(rtsl
    PRIVATE
        
        
)

set_target_properties(rtsl PROPERTIES
    CXX_EXTENSIONS OFF
)

add_executable(rtslc)

target_sources(rtslc
    PRIVATE
        
)

target_link_libraries(rtslc
    PRIVATE
        rtsl
)

target_compile_features(rtslc PRIVATE cxx_std_17)

target_include_directories(rtslc
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

set_target_properties(rtslc PROPERTIES
    CXX_EXTENSIONS OFF
)
