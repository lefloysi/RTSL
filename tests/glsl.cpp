#include "artifact/linker.hpp"
#include "driver/compiler.hpp"
#include "rtsl/glsl.hpp"
#include "rtsl/sdk.hpp"

#include <catch2/catch_test_macros.hpp>

#include <span>
#include <string>
#include <string_view>

using namespace rtsl;

namespace {

std::expected<Program, LoadError> compile_glsl_program(std::string_view source) {
	CompilerInstance compiler;
	Artifact object = compiler.compile_source(source, CompilerInvocation{ .source_name = "glsl_test.rtsl" });
	if (compiler.diagnostics().has_error()) {
		return std::unexpected(LoadError{ .message = compiler.diagnostics().diagnostics().front().message });
	}
	Linker linker{ compiler.diagnostics() };
	if (!linker.add_artifact(object)) {
		return std::unexpected(LoadError{ .message = "linker rejected object" });
	}
	Artifact artifact = linker.link_program();
	if (compiler.diagnostics().has_error()) {
		return std::unexpected(LoadError{ .message = compiler.diagnostics().diagnostics().front().message });
	}
	return load_program(std::as_bytes(std::span{ artifact.bytes }));
}

void require_transpiles(const Program& program, Stage stage) {
	auto shader = glsl::transpile(program, stage, glsl::Options{ .version = 400 });
	const std::string diagnostic = shader.has_value() ? shader->source : shader.error().message;
	INFO(diagnostic);
	REQUIRE(shader.has_value());
	REQUIRE(shader->entry_point == "main");
	REQUIRE_FALSE(shader->source.empty());
}

} // namespace

TEST_CASE("GLSL transpiler lowers structured control flow through state machine") {
	auto program = compile_glsl_program(
		"struct Point { vec3 position; };\n"
		"struct Vertex { vec4 position; f32 shade; };\n"
		"@stage : vertex fn vertex_entry(Point p) -> Vertex : position(clip), shade(flat) {\n"
		"    return Vertex(vec4(p.position, 1.0), 1.0);\n"
		"}\n"
		"@stage : fragment fn fragment_entry(Vertex v) -> vec4 {\n"
		"    f32 shade = 1.0;\n"
		"    if (v.shade < 0.5) { shade = 0.7; }\n"
		"    return vec4(shade, shade, shade, 1.0);\n"
		"}\n");
	REQUIRE(program.has_value());
	auto fragment = glsl::transpile(*program, Stage::fragment, glsl::Options{ .version = 400 });
	const std::string diagnostic = fragment.has_value() ? fragment->source : fragment.error().message;
	INFO(diagnostic);
	REQUIRE(fragment.has_value());
	REQUIRE(fragment->source.find("switch (rtsl_block)") != std::string::npos);
	REQUIRE(fragment->source.find("if (v") == std::string::npos);
}

TEST_CASE("GLSL transpiler emits called functions before callers") {
	auto program = compile_glsl_program(
		"uniform { readonly StorageBuffer colors; }\n"
		"layout colors : vec4[];\n"
		"struct Point { vec3 position; };\n"
		"struct Vertex { vec4 position; };\n"
		"fn color(u32 index) -> vec4 { return colors[index]; }\n"
		"fn choose(vec4 value, bool replace) -> vec4 { if (replace) { value.rgb = vec3(0.5); } return value; }\n"
		"fn shade(u32 index) -> vec4 { return choose(color(index) * 0.5, index == u32(0)); }\n"
		"@stage : vertex fn vertex_entry(Point p) -> Vertex : position(clip) { return Vertex(vec4(p.position, 1.0)); }\n"
		"@stage : fragment fn fragment_entry(Vertex v) -> vec4 { return shade(u32(0)); }\n");
	REQUIRE(program.has_value());
	REQUIRE(contains(program->resources()[0].stages, Stage::fragment));
	require_transpiles(*program, Stage::fragment);
}
