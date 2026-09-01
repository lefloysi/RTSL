find_package(Catch2 CONFIG REQUIRED)
include(Catch)

add_executable(rtsl-tests
	src/AST/CanonicalTypes.cpp
	src/Frontend/ModuleInterface.cpp
	src/Integration/Linker.cpp
	src/Integration/Bezier.cpp
	src/Integration/Triangle.cpp
	src/IR/CodeGen.cpp
	src/IR/Ownership.cpp
	src/IR/Serialization.cpp
	src/Parser/Declarations.cpp
)
target_link_libraries(rtsl-tests PRIVATE RTSL::rtsl Catch2::Catch2WithMain)
target_compile_definitions(rtsl-tests PRIVATE
	RTSL_TRIANGLE_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/inputs/Shaders/triangle.rtsl"
	RTSL_BEZIER_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/inputs/Shaders/bezier.rtsl")
catch_discover_tests(rtsl-tests
	TEST_PREFIX "rtsl."
	DISCOVERY_MODE PRE_TEST
)
