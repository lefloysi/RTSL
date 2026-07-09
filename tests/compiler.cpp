#include "artifact/artifact.hpp"
#include "driver/compiler.hpp"
#include "frontend/lexer.hpp"
#include "artifact/linker.hpp"
#include "sema/mangler.hpp"
#include "frontend/parser.hpp"
#include "rtsl.h"

#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <filesystem>
#include <fstream>

using namespace rtsl;
TEST_CASE("compiler honors basic preprocessor blocks") {
	CompilerInstance compiler;
	const char* source = R"(
#define ENABLED
#ifdef ENABLED
export fn helper() {}
#endif
)";
	auto artifact = compiler.compile_source(source, CompilerInvocation{ .source_name = "pp.rtsl" });
	REQUIRE_FALSE(compiler.diagnostics().has_error());
	REQUIRE(artifact.kind == ArtifactKind::object);
	REQUIRE_FALSE(artifact.bytes.empty());
}

TEST_CASE("compiler re-exports exported imports") {
	const auto dir = std::filesystem::temp_directory_path() / "rtsl-export-import-test";
	std::filesystem::create_directories(dir);
	const auto helper_path = dir / "helper.rtsl";
	{
		std::ofstream helper(helper_path, std::ios::binary);
		helper << "export fn helper() {}\n";
	}

	CompilerInstance compiler;
	Artifact forwarder;
	compiler.compile_source_to(
		forwarder,
		"export import <helper.rtsl>;\n",
		CompilerInvocation{
			.source_name = (dir / "forwarder.rtsl").string(),
			.import_paths = { dir.string() },
		}
	);
	REQUIRE_FALSE(compiler.diagnostics().has_error());

	const Artifact module = extract_module_interface(forwarder);
	REQUIRE_FALSE(module.bytes.empty());
	REQUIRE(module.exports.size() == 1);
	REQUIRE(module.exports[0].name == "helper");
}

TEST_CASE("compiler reports source import cycles") {
	const auto dir = std::filesystem::temp_directory_path() / "rtsl-import-cycle-test";
	std::filesystem::create_directories(dir);
	{
		std::ofstream a(dir / "a.rtsl", std::ios::binary);
		a << "import <b.rtsl>;\nexport fn a() {}\n";
	}
	{
		std::ofstream b(dir / "b.rtsl", std::ios::binary);
		b << "import <a.rtsl>;\nexport fn b() {}\n";
	}

	CompilerInstance compiler;
	auto artifact = compiler.compile_source(
		"import <a.rtsl>;\nexport fn root() {}\n",
		CompilerInvocation{
			.source_name = (dir / "root.rtsl").string(),
			.import_paths = { dir.string() },
		}
	);
	(void)artifact;
	REQUIRE(compiler.diagnostics().has_error());
}

TEST_CASE("compiler emits object") {
	CompilerInstance compiler;
	auto artifact = compiler.compile_source(
		"struct Point { vec3 position; };\n"
		"struct Vertex { vec3 position; };\n"
		"export fn make_vertex(Point p) -> Vertex { return Vertex(p.position); }\n",
		CompilerInvocation{ .source_name = "test.rtsl" }
	);
	REQUIRE_FALSE(compiler.diagnostics().has_error());
	REQUIRE(artifact.kind == ArtifactKind::object);
	REQUIRE_FALSE(artifact.bytes.empty());
	REQUIRE(artifact.module.functions.size() == 1);
	REQUIRE(artifact.module.functions.front().parameter_ids.size() == 1);
}

TEST_CASE("linker emits program") {
	CompilerInstance compiler;
	auto object = compiler.compile_source(
		"struct Point { vec3 position; };\n"
		"struct Vertex { vec4 position; };\n"
		"@vertex fn vertex_entry(Point p) -> Vertex : position(clip) { return Vertex(vec4(p.position, 1.0)); }\n"
		"@fragment fn fragment_entry(Vertex v) -> vec4 { return vec4(1.0, 0.0, 0.0, 1.0); }\n",
		CompilerInvocation{ .source_name = "test.rtsl" }
	);
	Linker linker(compiler.diagnostics());
	REQUIRE(linker.add_artifact(object));
	auto program = linker.link_program();
	REQUIRE_FALSE(compiler.diagnostics().has_error());
	REQUIRE(program.kind == ArtifactKind::program);
	REQUIRE_FALSE(program.bytes.empty());
}

TEST_CASE("program link rejects objects without stage entries") {
	CompilerInstance compiler;
	auto object = compiler.compile_source(
		"export fn helper() {}\n",
		CompilerInvocation{ .source_name = "library_shape.rtsl" }
	);
	REQUIRE_FALSE(compiler.diagnostics().has_error());

	Linker linker(compiler.diagnostics());
	REQUIRE(linker.add_artifact(object));
	auto program = linker.link_program();
	REQUIRE(compiler.diagnostics().has_error());
	REQUIRE(program.bytes.empty());
}

TEST_CASE("program link rejects incomplete graphics pipelines") {
	CompilerInstance compiler;
	auto object = compiler.compile_source(
		"struct Point { vec3 position; };\n"
		"struct Vertex { vec4 position; };\n"
		"@vertex fn vertex_entry(Point p) -> Vertex : position(clip) { return Vertex(vec4(p.position, 1.0)); }\n",
		CompilerInvocation{ .source_name = "vertex_only.rtsl" }
	);
	REQUIRE_FALSE(compiler.diagnostics().has_error());

	Linker linker(compiler.diagnostics());
	REQUIRE(linker.add_artifact(object));
	auto program = linker.link_program();
	REQUIRE(compiler.diagnostics().has_error());
	REQUIRE(program.bytes.empty());
}

TEST_CASE("program link rejects duplicate stage entries") {
	CompilerInstance compiler;
	auto object = compiler.compile_source(
		"struct Point { vec3 position; };\n"
		"struct Vertex { vec4 position; };\n"
		"@vertex fn vertex_a(Point p) -> Vertex : position(clip) { return Vertex(vec4(p.position, 1.0)); }\n"
		"@vertex fn vertex_b(Point p) -> Vertex : position(clip) { return Vertex(vec4(p.position, 1.0)); }\n"
		"@fragment fn fragment_entry(Vertex v) -> vec4 { return vec4(1.0, 0.0, 0.0, 1.0); }\n",
		CompilerInvocation{ .source_name = "duplicate_vertex.rtsl" }
	);
	REQUIRE_FALSE(compiler.diagnostics().has_error());

	Linker linker(compiler.diagnostics());
	REQUIRE(linker.add_artifact(object));
	auto program = linker.link_program();
	REQUIRE(compiler.diagnostics().has_error());
	REQUIRE(program.bytes.empty());
}

TEST_CASE("program link rejects unresolved imported calls") {
	const auto dir = std::filesystem::temp_directory_path() / "rtsl-unresolved-import-test";
	std::filesystem::create_directories(dir);
	{
		std::ofstream helper(dir / "helper.rtsl", std::ios::binary);
		helper << "export fn tint() -> vec4 { return vec4(1.0, 1.0, 1.0, 1.0); }\n";
	}

	CompilerInstance compiler;
	auto object = compiler.compile_source(
		"import <helper.rtsl>;\n"
		"struct Point { vec3 position; };\n"
		"struct Vertex { vec4 position; };\n"
		"@vertex fn vertex_entry(Point p) -> Vertex : position(clip) { return Vertex(vec4(p.position, 1.0)); }\n"
		"@fragment fn fragment_entry(Vertex v) -> vec4 { return tint(); }\n",
		CompilerInvocation{
			.source_name = (dir / "root.rtsl").string(),
			.import_paths = { dir.string() },
		}
	);
	REQUIRE_FALSE(compiler.diagnostics().has_error());

	Linker linker(compiler.diagnostics());
	REQUIRE(linker.add_artifact(object));
	auto program = linker.link_program();
	REQUIRE(compiler.diagnostics().has_error());
	REQUIRE(program.bytes.empty());
}

TEST_CASE("fragment bare vec4 reflects as default color output") {
	CompilerInstance compiler;
	auto object = compiler.compile_source(
		"struct Point { vec3 position; };\n"
		"struct Vertex { vec4 position; };\n"
		"@vertex fn vertex_entry(Point p) -> Vertex : position(clip) { return Vertex(vec4(p.position, 1.0)); }\n"
		"@fragment fn shade(Vertex v) -> vec4 { return vec4(1.0, 0.0, 0.0, 1.0); }\n",
		CompilerInvocation{ .source_name = "fragment_color.rtsl" }
	);
	REQUIRE_FALSE(compiler.diagnostics().has_error());
	REQUIRE_FALSE(object.bytes.empty());

	bool found_color_output = false;
	for (const auto& iface : object.module.stage_interfaces) {
		if (iface.role != StageRole::output || iface.type_name != "vec4") continue;
		REQUIRE(iface.fields.size() == 1);
		REQUIRE(iface.fields[0].name == "color");
		REQUIRE(iface.fields[0].location == 0);
		found_color_output = true;
	}
	REQUIRE(found_color_output);
}

TEST_CASE("C ABI lifetime and errors") {
	rtsl_context ctx = rtslCreateContext();
	REQUIRE(ctx);
	const char* source =
		"struct Point { vec3 position; };\n"
		"struct Vertex { vec4 position; };\n"
		"@vertex fn vertex_entry(Point p) -> Vertex : position(clip) { return Vertex(vec4(p.position, 1.0)); }\n"
		"@fragment fn fragment_entry(Vertex v) -> vec4 { return vec4(1.0, 0.0, 0.0, 1.0); }\n";
	rtsl_module object = rtslCompileSource(ctx, source, std::strlen(source), "abi.rtsl");
	REQUIRE(object);
	REQUIRE(rtslModuleGetBytecode(object).size > 0);
	REQUIRE(rtslModuleGetStageVariableCount(object) > 0);
	rtsl_stage_variable stage_var{};
	REQUIRE(rtslModuleGetStageVariable(object, 0, &stage_var));
	REQUIRE(stage_var.payload_type != nullptr);
	REQUIRE(std::strlen(stage_var.payload_type) > 0);

	rtsl_linker linker = rtslCreateLinker(ctx);
	REQUIRE(linker);
	REQUIRE(rtslLinkerAddModule(linker, object));
	rtsl_module library = rtslLinkLibrary(linker);
	REQUIRE(library);
	REQUIRE(rtslModuleGetKind(library) == RTSL_OUTPUT_LIBRARY);
	rtslDestroyModule(library);

	rtsl_module program = rtslLinkProgram(linker);
	REQUIRE(program);
	REQUIRE(rtslModuleGetKind(program) == RTSL_OUTPUT_PROGRAM);

	rtslDestroyModule(program);
	rtslDestroyLinker(linker);
	rtslDestroyModule(object);
	rtslDestroyContext(ctx);
}

TEST_CASE("compiler rejects unknown field types") {
	CompilerInstance compiler;
	auto artifact = compiler.compile_source(
		"struct Point { vec3 position; svec2 uv; };\n",
		CompilerInvocation{ .source_name = "bad_type.rtsl" }
	);
	(void)artifact;
	REQUIRE(compiler.diagnostics().has_error());
}

TEST_CASE("compiler rejects stray identifiers in function bodies") {
	CompilerInstance compiler;
	auto artifact = compiler.compile_source(
		"struct Point { vec3 position; }\n"
		"struct Vertex { vec4 position; }\n"
		"fn bad_body(Point p) -> Vertex {\n"
		"    random_identifier\n"
		"}\n",
		CompilerInvocation{ .source_name = "bad_body.rtsl" }
	);
	(void)artifact;
	REQUIRE(compiler.diagnostics().has_error());
}

TEST_CASE("compiler rejects no-effect expression statements") {
	CompilerInstance compiler;
	auto artifact = compiler.compile_source(
		"fn no_effect() {\n"
		"    s;\n"
		"}\n",
		CompilerInvocation{ .source_name = "no_effect_name.rtsl" }
	);
	(void)artifact;
	REQUIRE(compiler.diagnostics().has_error());
	REQUIRE(artifact.bytes.empty());
}

TEST_CASE("compiler rejects value-only binary expression statements") {
	CompilerInstance compiler;
	auto artifact = compiler.compile_source(
		"fn compare(f32 a, f32 b) {\n"
		"    a == b;\n"
		"}\n",
		CompilerInvocation{ .source_name = "no_effect_binary.rtsl" }
	);
	(void)artifact;
	REQUIRE(compiler.diagnostics().has_error());
	REQUIRE(artifact.bytes.empty());
}

TEST_CASE("compiler rejects unknown call targets") {
	CompilerInstance compiler;
	auto artifact = compiler.compile_source(
		"struct Point { vec3 position; }\n"
		"struct Vertex { vec4 position; }\n"
		"fn bad_call(Point p) -> Vertex {\n"
		"    return Vertexs(p);\n"
		"}\n",
		CompilerInvocation{ .source_name = "bad_call.rtsl" }
	);
	(void)artifact;
	REQUIRE(compiler.diagnostics().has_error());
}

TEST_CASE("compiler rejects struct construction that matches neither member declaration nor fields") {
	CompilerInstance compiler;
	auto artifact = compiler.compile_source(
		"struct Point { vec3 position; }\n"
		"struct Vertex { vec4 position; }\n"
		"fn bad_construct(Point p) -> Vertex {\n"
		"    return Vertex(p);\n"
		"}\n",
		CompilerInvocation{ .source_name = "bad_construct.rtsl" }
	);
	(void)artifact;
	REQUIRE(compiler.diagnostics().has_error());
	REQUIRE(artifact.bytes.empty());
}

TEST_CASE("compiler rejects out-of-line member definitions without in-type declaration") {
	CompilerInstance compiler;
	auto artifact = compiler.compile_source(
		"struct Vertex { vec4 position; }\n"
		"fn Vertex::Vertex(vec4 p) {\n"
		"    position = p;\n"
		"}\n",
		CompilerInvocation{ .source_name = "undeclared_member.rtsl" }
	);
	(void)artifact;
	REQUIRE(compiler.diagnostics().has_error());
	REQUIRE(artifact.bytes.empty());
}

TEST_CASE("compiler accepts out-of-line member definitions declared in the owner type") {
	CompilerInstance compiler;
	auto artifact = compiler.compile_source(
		"struct Vertex { vec4 position; fn Vertex(vec4 p); };\n"
		"fn Vertex::Vertex(vec4 p) {\n"
		"    position = p;\n"
		"}\n",
		CompilerInvocation{ .source_name = "declared_member.rtsl" }
	);
	REQUIRE_FALSE(compiler.diagnostics().has_error());
	REQUIRE_FALSE(artifact.bytes.empty());
}

TEST_CASE("compiler accepts inline member-init constructors") {
	CompilerInstance compiler;
	auto artifact = compiler.compile_source(
		"struct Vertex {\n"
		"    vec4 position;\n"
		"    fn Vertex(vec4 p) { position = p; }\n"
		"};\n"
		"fn make_vertex(vec4 p) -> Vertex {\n"
		"    return Vertex(p);\n"
		"}\n",
		CompilerInvocation{ .source_name = "inline_constructor.rtsl" }
	);
	REQUIRE_FALSE(compiler.diagnostics().has_error());
	REQUIRE_FALSE(artifact.bytes.empty());
}

TEST_CASE("compiler rejects invalid stage payload boundary tag") {
	CompilerInstance compiler;
	auto artifact = compiler.compile_source(
		"struct Vertex { vec4 position; };\n"
		"@vertex fn vertex_entry() -> Vertex : position(random_tag) { return Vertex(vec4(0.0, 0.0, 0.0, 1.0)); }\n",
		CompilerInvocation{ .source_name = "bad_boundary_tag.rtsl" }
	);
	(void)artifact;
	REQUIRE(compiler.diagnostics().has_error());
	REQUIRE(artifact.bytes.empty());
}

TEST_CASE("compiler accepts resource uniforms") {
	CompilerInstance compiler;
	auto artifact = compiler.compile_source(
		"uniform {\n"
		"    UniformBuffer mvp;\n"
		"}\n"
		"uniform albedo {\n"
		"    Sampler2D texture;\n"
		"    UniformBuffer tint;\n"
		"}\n"
		"layout mvp : mat4;\n"
		"layout albedo::tint : vec4;\n",
		CompilerInvocation{ .source_name = "resources.rtsl" }
	);
	REQUIRE_FALSE(compiler.diagnostics().has_error());
	REQUIRE_FALSE(artifact.bytes.empty());
}

TEST_CASE("compiler resolves using-imported uniform symbol") {
	CompilerInstance compiler;
	auto artifact = compiler.compile_source(
		"uniform albedo {\n"
		"    UniformBuffer tint;\n"
		"}\n"
		"layout albedo::tint : vec4;\n"
		"using albedo::tint;\n"
		"fn read_tint() -> vec4 {\n"
		"    return tint;\n"
		"}\n",
		CompilerInvocation{ .source_name = "using_uniform_symbol.rtsl" }
	);
	REQUIRE_FALSE(compiler.diagnostics().has_error());
	REQUIRE_FALSE(artifact.bytes.empty());
}

TEST_CASE("compiler resolves using-imported uniform scope") {
	CompilerInstance compiler;
	auto artifact = compiler.compile_source(
		"uniform albedo {\n"
		"    Sampler2D texture;\n"
		"    UniformBuffer tint;\n"
		"}\n"
		"layout albedo::tint : vec4;\n"
		"using uniform albedo;\n"
		"fn sample_tint(vec2 uv) -> vec4 {\n"
		"    return sample(texture, uv) * tint;\n"
		"}\n",
		CompilerInvocation{ .source_name = "using_uniform_scope.rtsl" }
	);
	REQUIRE_FALSE(compiler.diagnostics().has_error());
	REQUIRE_FALSE(artifact.bytes.empty());
}

TEST_CASE("compiler resolves qualified uniform scope members without using") {
	CompilerInstance compiler;
	auto artifact = compiler.compile_source(
		"uniform albedo {\n"
		"    Sampler2D texture;\n"
		"    UniformBuffer tint;\n"
		"}\n"
		"layout albedo::tint : vec4;\n"
		"fn sample_qualified(vec2 uv) -> vec4 {\n"
		"    return sample(albedo::texture, uv) * albedo::tint;\n"
		"}\n",
		CompilerInvocation{ .source_name = "qualified_uniform_scope.rtsl" }
	);
	REQUIRE_FALSE(compiler.diagnostics().has_error());
	REQUIRE_FALSE(artifact.bytes.empty());
}

TEST_CASE("compiler rejects unqualified access to named uniform scope members without using") {
	CompilerInstance compiler;
	auto artifact = compiler.compile_source(
		"uniform albedo {\n"
		"    Sampler2D texture;\n"
		"    UniformBuffer tint;\n"
		"}\n"
		"layout albedo::tint : vec4;\n"
		"fn sample_missing_using(vec2 uv) -> vec4 {\n"
		"    return sample(texture, uv) * tint;\n"
		"}\n",
		CompilerInvocation{ .source_name = "missing_using_uniform_scope.rtsl" }
	);
	(void)artifact;
	REQUIRE(compiler.diagnostics().has_error());
	REQUIRE(artifact.bytes.empty());
}
