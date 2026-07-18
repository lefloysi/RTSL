#include "artifact/linker.hpp"
#include "driver/compiler.hpp"
#include "rtsl/sdk.hpp"
#include "rtsl/spirv.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <span>
#include <string_view>

using namespace rtsl;

namespace {

std::expected<Program, LoadError> compile_program(std::string_view source) {
	CompilerInstance compiler;
	Artifact object = compiler.compile_source(source, CompilerInvocation{ .source_name = "spirv_test.rtsl" });
	if (compiler.diagnostics().has_error()) return std::unexpected(LoadError{ .message = "source compilation failed" });
	Linker linker{ compiler.diagnostics() };
	if (!linker.add_artifact(object)) return std::unexpected(LoadError{ .message = "linker rejected object" });
	Artifact artifact = linker.link_program();
	if (compiler.diagnostics().has_error()) return std::unexpected(LoadError{ .message = compiler.diagnostics().diagnostics().front().message });
	return load_program(std::as_bytes(std::span{ artifact.bytes }));
}

#ifdef RTSL_SPIRV_VAL
bool validates(const spirv::Shader& shader, std::string_view name) {
	const std::filesystem::path path = std::filesystem::temp_directory_path() /
		(std::string{ "rtsl-" } + std::string{ name } + ".spv");
	{
		std::ofstream output{ path, std::ios::binary };
		output.write(reinterpret_cast<const char*>(shader.words.data()),
			static_cast<std::streamsize>(shader.byte_size()));
	}
	const std::string command = RTSL_SPIRV_VAL " --target-env vulkan1.3 \"" + path.string() + "\"";
	return std::system(command.c_str()) == 0;
}
#endif

} // namespace

TEST_CASE("SPIR-V transpiler extracts and validates both graphics stages") {
	auto program = compile_program(
		"struct Point { vec3 position; vec2 uv; };\n"
		"struct Vertex { vec4 position; vec2 uv; };\n"
		"@stage : vertex fn vertex_entry(Point p) -> Vertex : position(clip), uv(smooth) {\n"
		"    return Vertex(vec4(p.position, 1.0), p.uv);\n"
		"}\n"
		"@stage : fragment fn fragment_entry(Vertex v) -> vec4 {\n"
		"    return vec4(v.uv, 0.0, 1.0);\n"
		"}\n");
	REQUIRE(program.has_value());

	auto vertex = spirv::transpile(*program, Stage::vertex);
	auto fragment = spirv::transpile(*program, Stage::fragment);
	REQUIRE(vertex.has_value());
	REQUIRE(fragment.has_value());
	REQUIRE(vertex->entry_point == "main");
	REQUIRE(fragment->entry_point == "main");
	REQUIRE(vertex->words.size() > 5);
	REQUIRE(fragment->words.size() > 5);
#ifdef RTSL_SPIRV_VAL
	REQUIRE(validates(*vertex, "vertex"));
	REQUIRE(validates(*fragment, "fragment"));
#endif
}

TEST_CASE("SPIR-V transpiler emits stage-local sampled texture resources") {
	auto program = compile_program(
		"struct Point { vec3 position; vec2 uv; };\n"
		"struct Vertex { vec4 position; vec2 uv; };\n"
		"uniform material { Sampler2D texture; }\n"
		"@stage : vertex fn vertex_entry(Point p) -> Vertex : position(clip), uv(smooth) {\n"
		"    return Vertex(vec4(p.position, 1.0), p.uv);\n"
		"}\n"
		"@stage : fragment fn fragment_entry(Vertex v) -> vec4 {\n"
		"    return sample(material::texture, v.uv);\n"
		"}\n");
	REQUIRE(program.has_value());
	REQUIRE(program->resources().size() == 1);
	REQUIRE_FALSE(contains(program->resources()[0].stages, Stage::vertex));
	REQUIRE(contains(program->resources()[0].stages, Stage::fragment));

	auto fragment = spirv::transpile(*program, Stage::fragment);
	REQUIRE(fragment.has_value());
#ifdef RTSL_SPIRV_VAL
	REQUIRE(validates(*fragment, "fragment-texture"));
#endif
}

TEST_CASE("SPIR-V transpiler validates normalized uniform and storage blocks") {
	auto program = compile_program(
		"struct Point { vec3 position; };\n"
		"struct Vertex { vec4 position; };\n"
		"uniform { UniformBuffer transform; }\n"
		"uniform material { readonly StorageBuffer tint; }\n"
		"layout transform : mat4;\n"
		"layout material::tint : vec4;\n"
		"@stage : vertex fn vertex_entry(Point p) -> Vertex : position(clip) {\n"
		"    return Vertex(transform * vec4(p.position, 1.0));\n"
		"}\n"
		"@stage : fragment fn fragment_entry(Vertex v) -> vec4 {\n"
		"    return material::tint;\n"
		"}\n");
	REQUIRE(program.has_value());
	REQUIRE(program->resources().size() == 2);
	REQUIRE(program->resources()[0].kind == ResourceKind::uniform_buffer);
	REQUIRE(program->resources()[1].kind == ResourceKind::storage_buffer);
	REQUIRE(contains(program->resources()[0].stages, Stage::vertex));
	REQUIRE_FALSE(contains(program->resources()[0].stages, Stage::fragment));
	REQUIRE_FALSE(contains(program->resources()[1].stages, Stage::vertex));
	REQUIRE(contains(program->resources()[1].stages, Stage::fragment));

	auto vertex = spirv::transpile(*program, Stage::vertex);
	auto fragment = spirv::transpile(*program, Stage::fragment);
	REQUIRE(vertex.has_value());
	REQUIRE(fragment.has_value());
#ifdef RTSL_SPIRV_VAL
	REQUIRE(validates(*vertex, "vertex-uniform-block"));
	REQUIRE(validates(*fragment, "fragment-storage-block"));
#endif
}

TEST_CASE("SPIR-V transpiler validates inline uniform block member access") {
	auto program = compile_program(
		"uniform { UniformBuffer scene; }\n"
		"layout scene : struct { mat4 transform; };\n"
		"struct Point { vec3 position; vec3 color; };\n"
		"struct Vertex { vec4 position; vec3 color; };\n"
		"@stage : vertex fn vertex_entry(Point p) -> Vertex : position(clip), color(flat) {\n"
		"    return Vertex(scene.transform * vec4(p.position, 1.0), p.color);\n"
		"}\n"
		"@stage : fragment fn fragment_entry(Vertex v) -> vec4 {\n"
		"    return vec4(v.color * 0.8, 1.0);\n"
		"}\n");
	REQUIRE(program.has_value());

	auto vertex = spirv::transpile(*program, Stage::vertex);
	auto fragment = spirv::transpile(*program, Stage::fragment);
	REQUIRE(vertex.has_value());
	REQUIRE(fragment.has_value());
#ifdef RTSL_SPIRV_VAL
	REQUIRE(validates(*vertex, "vertex-inline-uniform-block"));
	REQUIRE(validates(*fragment, "fragment-inline-uniform-block"));
#endif
}

TEST_CASE("SPIR-V transpiler validates terrain shader language surface") {
	auto program = compile_program(
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
	REQUIRE(program.has_value());
	auto fragment = spirv::transpile(*program, Stage::fragment);
	REQUIRE(fragment.has_value());
#ifdef RTSL_SPIRV_VAL
	REQUIRE(validates(*fragment, "fragment-terrain-surface"));
#endif
}

TEST_CASE("SPIR-V transpiler preserves and validates user function calls") {
	auto program = compile_program(
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
	auto fragment = spirv::transpile(*program, Stage::fragment);
	const std::string diagnostic = fragment.has_value() ? "transpiled" : fragment.error().message;
	INFO(diagnostic);
	REQUIRE(fragment.has_value());
#ifdef RTSL_SPIRV_VAL
	REQUIRE(validates(*fragment, "fragment-function-calls"));
#endif
}
