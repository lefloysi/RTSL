cmake_minimum_required(VERSION 3.20)

project(rtsl-package-consumer LANGUAGES CXX)

find_package(RTSL 0.1 CONFIG REQUIRED)

if(TARGET rtsl OR TARGET rtslc OR TARGET RTSL::compiler)
	message(FATAL_ERROR "The runtime package exported a compiler target")
endif()

add_executable(rtsl-package-consumer main.cpp)
target_compile_features(rtsl-package-consumer PRIVATE cxx_std_23)
target_link_libraries(rtsl-package-consumer PRIVATE RTSL::sdk RTSL::spirv)
