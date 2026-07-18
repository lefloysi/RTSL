#include "artifact/linker.hpp"
#include "driver/compiler.hpp"
#include "rtsl/hlsl.hpp"
#include "rtsl/sdk.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <string_view>

using namespace rtsl;

namespace {

std::expected<Program, LoadError> compile_hlsl_program(std::string_view source) {
	CompilerInstance compiler;
	Artifact object = compiler.compile_source(source, CompilerInvocation{ .source_name = "hlsl_test.rtsl" });
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

#ifdef RTSL_DXC
bool compiles(const hlsl::Shader& shader, std::string_view name) {
	const std::filesystem::path source_path = std::filesystem::temp_directory_path() /
		(std::string{ "rtsl-" } + std::string{ name } + ".hlsl");
	const std::filesystem::path output_path = source_path.parent_path() /
		(std::string{ "rtsl-" } + std::string{ name } + ".dxil");
	{
		std::ofstream output{ source_path };
		output << shader.source;
	}
	const char* profile = shader.stage == Stage::vertex ? "vs_6_0" : "ps_6_0";
	const std::string command = RTSL_DXC " -E main -T " + std::string{ profile } +
		" -HV 2021 -Ges -Fo \"" + output_path.string() + "\" \"" + source_path.string() + "\"";
	return std::system(command.c_str()) == 0;
}
#endif

void require_compiles(const Program& program, Stage stage, std::string_view name) {
	auto shader = hlsl::transpile(program, stage);
	const std::string diagnostic = shader.has_value() ? shader->source : shader.error().message;
	INFO(diagnostic);
	REQUIRE(shader.has_value());
	REQUIRE(shader->entry_point == "main");
	REQUIRE_FALSE(shader->source.empty());
#ifdef RTSL_DXC
	REQUIRE(compiles(*shader, name));
#endif
}

} // namespace

TEST_CASE("HLSL transpiler compiles both graphics stages") {
	auto program = compile_hlsl_program(
		"struct Point { vec3 position; vec2 uv; };\n"
		"struct Vertex { vec4 position; vec2 uv; };\n"
		"@stage : vertex fn vertex_entry(Point p) -> Vertex : position(clip), uv(smooth) {\n"
		"    return Vertex(vec4(p.position, 1.0), p.uv);\n"
		"}\n"
		"@stage : fragment fn fragment_entry(Vertex v) -> vec4 {\n"
		"    return vec4(v.uv, 0.0, 1.0);\n"
		"}\n");
	REQUIRE(program.has_value());
	require_compiles(*program, Stage::vertex, "vertex");
	require_compiles(*program, Stage::fragment, "fragment");
	const auto fragment = hlsl::transpile(*program, Stage::fragment);
	REQUIRE(fragment.has_value());
	REQUIRE(fragment->source.find("float4 position : SV_Position;") != std::string::npos);
}

TEST_CASE("HLSL transpiler compiles uniform and sampled texture resources") {
	auto program = compile_hlsl_program(
		"uniform { UniformBuffer scene; Sampler2D texture; }\n"
		"layout scene : struct { mat4 transform; };\n"
		"struct Point { vec3 position; vec2 uv; };\n"
		"struct Vertex { vec4 position; vec2 uv; };\n"
		"@stage : vertex fn vertex_entry(Point p) -> Vertex : position(clip), uv(smooth) {\n"
		"    return Vertex(scene.transform * vec4(p.position, 1.0), p.uv);\n"
		"}\n"
		"@stage : fragment fn fragment_entry(Vertex v) -> vec4 {\n"
		"    return sample(texture, v.uv);\n"
		"}\n");
	REQUIRE(program.has_value());
	require_compiles(*program, Stage::vertex, "vertex-resources");
	require_compiles(*program, Stage::fragment, "fragment-resources");
}

TEST_CASE("HLSL transpiler compiles structured control flow") {
	auto program = compile_hlsl_program(
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
	const std::string diagnostic = program.has_value() ? "loaded" : program.error().context + ": " + program.error().message;
	INFO(diagnostic);
	REQUIRE(program.has_value());
	require_compiles(*program, Stage::fragment, "fragment-control-flow");
}

TEST_CASE("HLSL transpiler compiles terrain shader language surface") {
	auto program = compile_hlsl_program(
		"uniform { readonly StorageBuffer cells; Sampler2D atlas; }\n"
		"layout cells : uvec4[];\n"
		"struct Point { vec3 position; vec2 uv; };\n"
		"struct Vertex { vec4 position; vec2 uv; };\n"
		"@stage : vertex fn vertex_entry(Point p) -> Vertex : position(clip), uv(smooth) {\n"
		"    return Vertex(vec4(p.position, 1.0), p.uv);\n"
		"}\n"
		"@stage : fragment fn fragment_entry(Vertex v) -> vec4 {\n"
		"    uvec2 bits = float_bits_to_uint(abs(v.uv));\n"
		"    u32 mask = (~u32(0) | u32(1)) ^ u32(2);\n"
		"    u32 index = (bits.x + u32(1)) & mask & u32(3);\n"
		"    uvec4 packed = cells[index];\n"
		"    vec2 dimensions = vec2(texture_size(atlas));\n"
		"    f32 value = sqrt(max(0.0, min(1.0, mod(f32(packed.x), dimensions.x))));\n"
		"    f32 wave = smoothstep(0.0, 1.0, fract(abs(v.uv.y))) + floor(v.uv.x);\n"
		"    return mix(sample(atlas, v.uv), vec4(value), wave);\n"
		"}\n");
	const std::string diagnostic = program.has_value() ? "loaded" : program.error().context + ": " + program.error().message;
	INFO(diagnostic);
	REQUIRE(program.has_value());
	require_compiles(*program, Stage::fragment, "fragment-terrain-surface");
}
