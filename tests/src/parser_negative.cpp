#include "driver/compiler.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace rtsl;

TEST_CASE("parser rejects dangling attribute markers") {
	CompilerInstance compiler;
	auto artifact = compiler.compile_source(
		"@stage : vertex\n"
		"fn vertex_entry(Point p) -> Vertex : position(clip), uv(smooth), material(flat) {\n"
		"    return Vertex(p);\n"
		"}\n"
		"@\n",
		CompilerInvocation{ .source_name = "dangling_attr.rtsl" }
	);
	(void)artifact;
	REQUIRE(compiler.diagnostics().has_error());
}

TEST_CASE("parser rejects malformed statement fragments") {
	CompilerInstance compiler;
	auto artifact = compiler.compile_source(
		"struct Point { vec3 position; }\n"
		"struct Vertex { vec4 position; }\n"
		"fn malformed(Point p) -> Vertex {\n"
		"    ss return Vertex(p);\n"
		"}\n",
		CompilerInvocation{ .source_name = "bad_stmt.rtsl" }
	);
	(void)artifact;
	REQUIRE(compiler.diagnostics().has_error());
}

TEST_CASE("parser rejects inout parameter qualifier") {
	CompilerInstance compiler;
	auto artifact = compiler.compile_source(
		"fn bad(inout vec4 color) {}\n",
		CompilerInvocation{ .source_name = "bad_inout.rtsl" }
	);
	(void)artifact;
	REQUIRE(compiler.diagnostics().has_error());
}

TEST_CASE("parser recovers after stray identifier before next declaration") {
	CompilerInstance compiler;
	auto artifact = compiler.compile_source(
		"struct Point { vec3 position; }\n"
		"struct Vertex { vec4 position; }\n"
		"fn recover(Point p) -> Vertex {\n"
		"    material = 0;\n"
		"    s\n"
		"    struct Fragment {\n"
		"        vec4 color;\n"
		"    }\n"
		"}\n",
		CompilerInvocation{ .source_name = "recover.rtsl" }
	);
	(void)artifact;
	REQUIRE(compiler.diagnostics().has_error());
}

TEST_CASE("parser stops at next declaration after missing semicolon") {
	CompilerInstance compiler;
	auto artifact = compiler.compile_source(
		"struct Point { vec3 position; }\n"
		"struct Vertex { vec4 position; }\n"
		"fn sync(Point p) -> Vertex {\n"
		"    material = 0;\n"
		"    s\n"
		"struct Fragment {\n"
		"    vec4 color;\n"
		"}\n",
		CompilerInvocation{ .source_name = "sync.rtsl" }
	);
	(void)artifact;
	REQUIRE(compiler.diagnostics().has_error());
}
