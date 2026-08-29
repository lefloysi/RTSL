cmake_minimum_required(VERSION 3.28)
project(RTSL VERSION 0.1.0 LANGUAGES C CXX)

enable_testing()

add_subdirectory("rtsl-core")
add_subdirectory("rtsl")
add_subdirectory("rtslc")
add_subdirectory("tests")

