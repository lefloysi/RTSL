add_library(rtsl-glsl STATIC
	src/glsl.cpp
)
add_library(RTSL::glsl ALIAS rtsl-glsl)

target_compile_features(rtsl-glsl PUBLIC cxx_std_23)
set_target_properties(rtsl-glsl PROPERTIES
	CXX_EXTENSIONS OFF
	EXPORT_NAME glsl
	POSITION_INDEPENDENT_CODE ON
)

target_include_directories(rtsl-glsl
	PUBLIC
		"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
		"$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)

target_link_libraries(rtsl-glsl PUBLIC RTSL::sdk)
