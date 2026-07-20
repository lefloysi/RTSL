include(RutileDependencies OPTIONAL)
if(COMMAND rutile_find_spirv_headers)
	rutile_find_spirv_headers()
else()
	find_package(SPIRV-Headers CONFIG REQUIRED)
endif()

add_library(rtsl-spirv STATIC
	src/spirv.cpp
)
add_library(RTSL::spirv ALIAS rtsl-spirv)

target_compile_features(rtsl-spirv PUBLIC cxx_std_23)
set_target_properties(rtsl-spirv PROPERTIES
	CXX_EXTENSIONS OFF
	EXPORT_NAME spirv
	POSITION_INDEPENDENT_CODE ON
)

target_include_directories(rtsl-spirv
	PUBLIC
		"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
		"$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)

# Public because the transpiler API accepts rtsl::Program. There is no
# compiler dependency: rtsl-spirv is an SDK consumer just like a backend.
target_link_libraries(rtsl-spirv
	PUBLIC RTSL::sdk
	PRIVATE SPIRV-Headers::SPIRV-Headers
)
