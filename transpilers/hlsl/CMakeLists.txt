add_library(rtsl-hlsl STATIC
	src/hlsl.cpp
)
add_library(RTSL::hlsl ALIAS rtsl-hlsl)

target_compile_features(rtsl-hlsl PUBLIC cxx_std_23)
set_target_properties(rtsl-hlsl PROPERTIES
	CXX_EXTENSIONS OFF
	EXPORT_NAME hlsl
	POSITION_INDEPENDENT_CODE ON
)

target_include_directories(rtsl-hlsl
	PUBLIC
		"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
		"$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)

target_link_libraries(rtsl-hlsl PUBLIC RTSL::sdk)
