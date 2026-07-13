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
		"@stage : vertex fn vertex_entry(Point p) -> Vertex : position(clip) { return Vertex(vec4(p.position, 1.0)); }\n"
		"@stage : fragment fn fragment_entry(Vertex v) -> vec4 { return vec4(1.0, 0.0, 0.0, 1.0); }\n",
		CompilerInvocation{ .source_name = "test.rtsl" }
	);
	Linker linker(compiler.diagnostics());
	REQUIRE(linker.add_artifact(object));
	auto program = linker.link_program();
	REQUIRE_FALSE(compiler.diagnostics().has_error());
	REQUIRE(program.kind == ArtifactKind::program);
	REQUIRE_FALSE(program.bytes.empty());

	Artifact loaded;
	REQUIRE(read_artifact(program.bytes, loaded));
	REQUIRE(loaded.module.call_targets.empty());
	for (const auto& fn : loaded.module.functions) {
		for (const auto& inst : fn.body) {
			REQUIRE(inst.op != IROp::FunctionCall);
		}
	}
}

TEST_CASE("linker resolves calls to reference-qualified functions") {
	CompilerInstance compiler;
	auto object = compiler.compile_source(
		"struct Point { vec3 position; vec2 uv; };\n"
		"struct Vertex { vec4 position; vec2 uv; fn Vertex(const Point& p); };\n"
		"fn Vertex::Vertex(const Point& p) { position = vec4(p.position, 1.0); uv = p.uv; }\n"
		"fn make_vertex(const Point& p) -> Vertex { return Vertex(p); }\n"
		"@stage : vertex fn vertex_entry(Point p) -> Vertex : position(clip), uv(smooth) { return make_vertex(p); }\n"
		"@stage : fragment fn fragment_entry(Vertex v) -> vec4 { return vec4(v.uv, 0.0, 1.0); }\n",
		CompilerInvocation{ .source_name = "reference_call_targets.rtsl" }
	);
	Linker linker(compiler.diagnostics());
	REQUIRE(linker.add_artifact(object));
	auto program = linker.link_program();
	REQUIRE_FALSE(compiler.diagnostics().has_error());
	REQUIRE(program.kind == ArtifactKind::program);
	REQUIRE_FALSE(program.bytes.empty());

	Artifact loaded;
	REQUIRE(read_artifact(program.bytes, loaded));
	REQUIRE(loaded.module.call_targets.empty());
	for (const auto& fn : loaded.module.functions) {
		for (const auto& inst : fn.body) {
			REQUIRE(inst.op != IROp::FunctionCall);
		}
	}
}

TEST_CASE("compiler rejects reference parameters on stage entries") {
	CompilerInstance compiler;
	auto artifact = compiler.compile_source(
		"struct Point { vec3 position; };\n"
		"struct Vertex { vec4 position; };\n"
		"@stage : vertex fn vertex_entry(const Point& p) -> Vertex : position(clip) { return Vertex(vec4(p.position, 1.0)); }\n"
		"@stage : fragment fn fragment_entry(Vertex v) -> vec4 { return vec4(1.0, 0.0, 0.0, 1.0); }\n",
		CompilerInvocation{ .source_name = "stage_reference_param.rtsl" }
	);
	(void)artifact;
	REQUIRE(compiler.diagnostics().has_error());
	REQUIRE(artifact.bytes.empty());
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
		"@stage : vertex fn vertex_entry(Point p) -> Vertex : position(clip) { return Vertex(vec4(p.position, 1.0)); }\n",
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
		"@stage : vertex fn vertex_a(Point p) -> Vertex : position(clip) { return Vertex(vec4(p.position, 1.0)); }\n"
		"@stage : vertex fn vertex_b(Point p) -> Vertex : position(clip) { return Vertex(vec4(p.position, 1.0)); }\n"
		"@stage : fragment fn fragment_entry(Vertex v) -> vec4 { return vec4(1.0, 0.0, 0.0, 1.0); }\n",
		CompilerInvocation{ .source_name = "duplicate_vertex.rtsl" }
	);
	REQUIRE_FALSE(compiler.diagnostics().has_error());

	Linker linker(compiler.diagnostics());
	REQUIRE(linker.add_artifact(object));
	auto program = linker.link_program();
	REQUIRE(compiler.diagnostics().has_error());
	REQUIRE(program.bytes.empty());
}

TEST_CASE("linker rejects duplicate exported function identities") {
	CompilerInstance compiler;
	auto first = compiler.compile_source(
		"export fn helper() -> vec4 { return vec4(1.0, 0.0, 0.0, 1.0); }\n",
		CompilerInvocation{ .source_name = "first.rtsl" }
	);
	REQUIRE_FALSE(compiler.diagnostics().has_error());
	auto second = compiler.compile_source(
		"export fn helper() -> vec4 { return vec4(0.0, 1.0, 0.0, 1.0); }\n",
		CompilerInvocation{ .source_name = "second.rtsl" }
	);
	REQUIRE_FALSE(compiler.diagnostics().has_error());

	Linker linker(compiler.diagnostics());
	REQUIRE(linker.add_artifact(first));
	REQUIRE(linker.add_artifact(second));
	auto library = linker.link_library();
	REQUIRE(compiler.diagnostics().has_error());
	REQUIRE(library.bytes.empty());
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
		"@stage : vertex fn vertex_entry(Point p) -> Vertex : position(clip) { return Vertex(vec4(p.position, 1.0)); }\n"
		"@stage : fragment fn fragment_entry(Vertex v) -> vec4 { return tint(); }\n",
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

TEST_CASE("linker rejects stale imported module interfaces") {
	const auto dir = std::filesystem::temp_directory_path() / "rtsl-stale-interface-test";
	std::filesystem::create_directories(dir);

	CompilerInstance interface_compiler;
	auto old_helper = interface_compiler.compile_source(
		"export fn tint() -> vec4 { return vec4(1.0, 1.0, 1.0, 1.0); }\n",
		CompilerInvocation{ .source_name = (dir / "helper.rtsl").string() }
	);
	REQUIRE_FALSE(interface_compiler.diagnostics().has_error());
	const Artifact old_interface = extract_module_interface(old_helper);
	{
		std::ofstream sidecar(dir / "helper.rtslm", std::ios::binary);
		sidecar.write(reinterpret_cast<const char*>(old_interface.bytes.data()), static_cast<std::streamsize>(old_interface.bytes.size()));
	}

	CompilerInstance compiler;
	auto root = compiler.compile_source(
		"import <helper.rtsl>;\n"
		"struct Point { vec3 position; };\n"
		"struct Vertex { vec4 position; };\n"
		"@stage : vertex fn vertex_entry(Point p) -> Vertex : position(clip) { return Vertex(vec4(p.position, 1.0)); }\n"
		"@stage : fragment fn fragment_entry(Vertex v) -> vec4 { return tint(); }\n",
		CompilerInvocation{
			.source_name = (dir / "root.rtsl").string(),
			.import_paths = { dir.string() },
		}
	);
	REQUIRE_FALSE(compiler.diagnostics().has_error());

	auto changed_helper = compiler.compile_source(
		"export fn tint() -> f32 { return 1.0; }\n",
		CompilerInvocation{ .source_name = (dir / "helper.rtsl").string() }
	);
	REQUIRE_FALSE(compiler.diagnostics().has_error());

	Linker linker(compiler.diagnostics());
	REQUIRE(linker.add_artifact(root));
	REQUIRE(linker.add_artifact(changed_helper));
	auto program = linker.link_program();
	REQUIRE(compiler.diagnostics().has_error());
	REQUIRE(program.bytes.empty());
}

TEST_CASE("fragment bare vec4 reflects as default color output") {
	CompilerInstance compiler;
	auto object = compiler.compile_source(
		"struct Point { vec3 position; };\n"
		"struct Vertex { vec4 position; };\n"
		"@stage : vertex fn vertex_entry(Point p) -> Vertex : position(clip) { return Vertex(vec4(p.position, 1.0)); }\n"
		"@stage : fragment fn shade(Vertex v) -> vec4 { return vec4(1.0, 0.0, 0.0, 1.0); }\n",
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

TEST_CASE("compiler lowers namespaced graphics program") {
	CompilerInstance compiler;
	auto object = compiler.compile_source(
		"namespace shader {\n"
		"    struct Point { vec3 position; vec2 uv; };\n"
		"    struct Vertex { vec4 position; vec2 uv; };\n"
		"    fn make_vertex(Point p) -> Vertex { return Vertex(vec4(p.position, 1.0), p.uv); }\n"
		"    @stage : vertex fn vertex_entry(Point p) -> Vertex : position(clip), uv(smooth) { return make_vertex(p); }\n"
		"    @stage : fragment fn fragment_entry(Vertex v) -> vec4 { return vec4(v.uv, 0.0, 1.0); }\n"
		"}\n",
		CompilerInvocation{ .source_name = "namespaced_graphics.rtsl" }
	);
	REQUIRE_FALSE(compiler.diagnostics().has_error());
	REQUIRE_FALSE(object.bytes.empty());

	Linker linker(compiler.diagnostics());
	REQUIRE(linker.add_artifact(object));
	auto program = linker.link_program();
	REQUIRE_FALSE(compiler.diagnostics().has_error());
	REQUIRE(program.kind == ArtifactKind::program);
	REQUIRE_FALSE(program.bytes.empty());
}

TEST_CASE("C ABI lifetime and errors") {
	rtsl_context ctx = rtslCreateContext();
	REQUIRE(ctx);
	const char* source =
		"struct Point { vec3 position; };\n"
		"struct Vertex { vec4 position; };\n"
		"@stage : vertex fn vertex_entry(Point p) -> Vertex : position(clip) { return Vertex(vec4(p.position, 1.0)); }\n"
		"@stage : fragment fn fragment_entry(Vertex v) -> vec4 { return vec4(1.0, 0.0, 0.0, 1.0); }\n";
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
	rtsl_blob program_bytes = rtslModuleGetBytecode(program);
	REQUIRE(program_bytes.size > 0);

	rtsl_module loaded_program = rtslLoadModule(ctx, program_bytes.data, program_bytes.size);
	REQUIRE(loaded_program);
	REQUIRE(rtslModuleGetKind(loaded_program) == RTSL_OUTPUT_PROGRAM);
	REQUIRE(rtslModuleGetEntryCount(loaded_program) == 2);
	REQUIRE(rtslModuleGetStageVariableCount(loaded_program) > 0);
	rtsl_entry_info first_entry{};
	rtsl_entry_info second_entry{};
	REQUIRE(rtslModuleGetEntry(loaded_program, 0, &first_entry));
	REQUIRE(rtslModuleGetEntry(loaded_program, 1, &second_entry));
	REQUIRE(first_entry.name != nullptr);
	REQUIRE(second_entry.name != nullptr);
	const bool has_vert = std::strcmp(first_entry.name, "vert") == 0 || std::strcmp(second_entry.name, "vert") == 0;
	const bool has_frag = std::strcmp(first_entry.name, "frag") == 0 || std::strcmp(second_entry.name, "frag") == 0;
	REQUIRE(has_vert);
	REQUIRE(has_frag);

	rtslDestroyModule(loaded_program);
	rtslDestroyModule(program);
	rtslDestroyLinker(linker);
	rtslDestroyModule(object);
	rtslDestroyContext(ctx);
}

TEST_CASE("C ABI reflects uniforms from loaded linked program") {
	rtsl_context ctx = rtslCreateContext();
	REQUIRE(ctx);
	const char* source =
		"struct Point { vec3 position; vec2 uv; };\n"
		"struct Vertex { vec4 position; vec2 uv; };\n"
		"uniform albedo {\n"
		"    Sampler2D texture;\n"
		"    UniformBuffer tint;\n"
		"}\n"
		"layout albedo::tint : vec4;\n"
		"@stage : vertex fn vertex_entry(Point p) -> Vertex : position(clip), uv(smooth) { return Vertex(vec4(p.position, 1.0), p.uv); }\n"
		"@stage : fragment fn fragment_entry(Vertex v) -> vec4 { return sample(albedo::texture, v.uv) * albedo::tint; }\n";
	rtsl_module object = rtslCompileSource(ctx, source, std::strlen(source), "abi_uniforms.rtsl");
	REQUIRE(object);
	rtsl_linker linker = rtslCreateLinker(ctx);
	REQUIRE(linker);
	REQUIRE(rtslLinkerAddModule(linker, object));
	rtsl_module program = rtslLinkProgram(linker);
	REQUIRE(program);
	const rtsl_blob bytes = rtslModuleGetBytecode(program);
	REQUIRE(bytes.size > 0);
	rtsl_module loaded = rtslLoadModule(ctx, bytes.data, bytes.size);
	REQUIRE(loaded);

	REQUIRE(rtslModuleGetUniformCount(loaded) == 2);
	rtsl_uniform_info first{};
	rtsl_uniform_info second{};
	REQUIRE(rtslModuleGetUniform(loaded, 0, &first));
	REQUIRE(rtslModuleGetUniform(loaded, 1, &second));
	REQUIRE(std::strcmp(first.qualified_name, "albedo.texture") == 0);
	REQUIRE(first.kind == RTSL_UNIFORM_KIND_SAMPLED_IMAGE);
	REQUIRE(first.group == 0);
	REQUIRE(first.member == 0);
	REQUIRE(std::strcmp(second.qualified_name, "albedo.tint") == 0);
	REQUIRE(second.kind == RTSL_UNIFORM_KIND_UNIFORM_BUFFER);
	REQUIRE(second.group == 0);
	REQUIRE(second.member == 1);

	rtslDestroyModule(loaded);
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
		"@stage : vertex fn vertex_entry() -> Vertex : position(random_tag) { return Vertex(vec4(0.0, 0.0, 0.0, 1.0)); }\n",
		CompilerInvocation{ .source_name = "bad_boundary_tag.rtsl" }
	);
	(void)artifact;
	REQUIRE(compiler.diagnostics().has_error());
	REQUIRE(artifact.bytes.empty());
}

TEST_CASE("compiler rejects unknown function attributes") {
	CompilerInstance compiler;
	auto artifact = compiler.compile_source(
		"@compute fn entry() {}\n",
		CompilerInvocation{ .source_name = "unknown_attr.rtsl" }
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

TEST_CASE("compiler classifies every declared resource type") {
	CompilerInstance compiler;
	auto artifact = compiler.compile_source(
		"uniform resources {\n"
		"    Sampler sampler;\n"
		"    Sampler2D tex2d;\n"
		"    Sampler3D tex3d;\n"
		"    SamplerCube cube;\n"
		"    Sampler2DArray array_tex;\n"
		"    Image2D image2d;\n"
		"    Image3D image3d;\n"
		"}\n",
		CompilerInvocation{ .source_name = "all_resources.rtsl" }
	);
	REQUIRE_FALSE(compiler.diagnostics().has_error());
	REQUIRE_FALSE(artifact.bytes.empty());
	REQUIRE(artifact.uniforms.size() == 7);
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

TEST_CASE("compiler resolves namespace-imported uniform scope") {
	CompilerInstance compiler;
	auto artifact = compiler.compile_source(
		"uniform albedo {\n"
		"    Sampler2D texture;\n"
		"    UniformBuffer tint;\n"
		"}\n"
		"layout albedo::tint : vec4;\n"
		"using namespace albedo;\n"
		"fn sample_tint(vec2 uv) -> vec4 {\n"
		"    return sample(texture, uv) * tint;\n"
		"}\n",
		CompilerInvocation{ .source_name = "using_namespace_uniform_scope.rtsl" }
	);
	REQUIRE_FALSE(compiler.diagnostics().has_error());
	REQUIRE_FALSE(artifact.bytes.empty());
}

TEST_CASE("compiler rejects using uniform scope syntax") {
	CompilerInstance compiler;
	auto artifact = compiler.compile_source(
		"uniform albedo {\n"
		"    UniformBuffer tint;\n"
		"}\n"
		"using uniform albedo;\n",
		CompilerInvocation{ .source_name = "using_uniform_removed.rtsl" }
	);
	(void)artifact;
	REQUIRE(compiler.diagnostics().has_error());
	REQUIRE(artifact.bytes.empty());
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

TEST_CASE("type checker infers uniform buffer value type from layout") {
	CompilerInstance compiler;
	auto artifact = compiler.compile_source(
		"uniform albedo {\n"
		"    UniformBuffer tint;\n"
		"}\n"
		"layout albedo::tint : vec4;\n"
		"fn read_tint() -> vec4 {\n"
		"    return albedo::tint;\n"
		"}\n",
		CompilerInvocation{ .source_name = "uniform_value_type.rtsl" }
	);
	REQUIRE_FALSE(compiler.diagnostics().has_error());
	REQUIRE_FALSE(artifact.bytes.empty());
}

TEST_CASE("type checker rejects uniform buffer value type mismatch") {
	CompilerInstance compiler;
	auto artifact = compiler.compile_source(
		"uniform albedo {\n"
		"    UniformBuffer tint;\n"
		"}\n"
		"layout albedo::tint : vec4;\n"
		"fn read_tint() -> f32 {\n"
		"    return albedo::tint;\n"
		"}\n",
		CompilerInvocation{ .source_name = "uniform_value_mismatch.rtsl" }
	);
	(void)artifact;
	REQUIRE(compiler.diagnostics().has_error());
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

namespace {
bool has_diagnostic_code(CompilerInstance& compiler, DiagnosticCode code) {
	for (const auto& diagnostic : compiler.diagnostics().diagnostics()) {
		if (diagnostic.code == static_cast<int>(code)) {
			return true;
		}
	}
	return false;
}
} // namespace

TEST_CASE("type checker rejects an unknown struct field type") {
	CompilerInstance compiler;
	auto artifact = compiler.compile_source(
		"struct Vertex { Vecc4 position; };\n",
		CompilerInvocation{ .source_name = "bad_field.rtsl" }
	);
	(void)artifact;
	REQUIRE(compiler.diagnostics().has_error());
	REQUIRE(has_diagnostic_code(compiler, DiagnosticCode::sema_unknown_type));
}

TEST_CASE("type checker rejects unknown parameter, return, and local types") {
	CompilerInstance compiler;
	auto artifact = compiler.compile_source(
		"fn f(Poynt p) -> Retn {\n"
		"    Flaot x = 1.0;\n"
		"    return x;\n"
		"}\n",
		CompilerInvocation{ .source_name = "bad_fn.rtsl" }
	);
	(void)artifact;
	REQUIRE(compiler.diagnostics().has_error());
	REQUIRE(has_diagnostic_code(compiler, DiagnosticCode::sema_unknown_type));
}

namespace {
const StageInterface* find_interface_by(const Artifact& artifact, std::string_view type_name, StageRole role) {
	for (const auto& interface : artifact.stage_interfaces) {
		if (interface.type_name == type_name && interface.role == role) {
			return &interface;
		}
	}
	return nullptr;
}
const StageIOField* find_field(const StageInterface& interface, std::string_view name) {
	for (const auto& field : interface.fields) {
		if (field.name == name) {
			return &field;
		}
	}
	return nullptr;
}
} // namespace

TEST_CASE("fragment output interface is distinct from same-named varying input") {
	CompilerInstance compiler;
	auto object = compiler.compile_source(
		"struct Point { vec3 position; };\n"
		"struct Payload { vec4 position; vec4 color; };\n"
		"@stage : vertex fn vertex_entry(Point p) -> Payload : position(clip), color(smooth) { return Payload(vec4(p.position, 1.0), vec4(1.0, 0.0, 0.0, 1.0)); }\n"
		"@stage : fragment fn fragment_entry(Payload p) -> Payload { return p; }\n",
		CompilerInvocation{ .source_name = "same_payload_output.rtsl" }
	);
	REQUIRE_FALSE(compiler.diagnostics().has_error());

	const StageInterface* varying = find_interface_by(object, "Payload", StageRole::varying);
	REQUIRE(varying != nullptr);
	const StageInterface* output = find_interface_by(object, "Payload", StageRole::output);
	REQUIRE(output != nullptr);
	REQUIRE(output->fields.size() == 2);
	REQUIRE(find_field(*output, "position") != nullptr);
	REQUIRE(find_field(*output, "color") != nullptr);
}

TEST_CASE("stage interface fields carry their struct member index") {
	CompilerInstance compiler;
	auto artifact = compiler.compile_source(
		"struct Point { vec3 position; vec2 uv; };\n"
		"struct Vertex { vec4 position; vec2 uv; u32 material; fn Vertex(Point p); };\n"
		"fn Vertex::Vertex(Point p) { position = vec4(p.position, 1.0); uv = p.uv; material = 0; }\n"
		"@stage : vertex fn vs(Point p) -> Vertex : position(clip), uv(smooth), material(flat) { return Vertex(p); }\n",
		CompilerInvocation{ .source_name = "member_index.rtsl" }
	);
	REQUIRE_FALSE(compiler.diagnostics().has_error());

	const auto check_indices = [](const Artifact& art) {
		// Input struct (synthesized): members map to their struct positions.
		const StageInterface* input = find_interface_by(art, "Point", StageRole::input);
		REQUIRE(input != nullptr);
		REQUIRE(find_field(*input, "position")->member_index == 0);
		REQUIRE(find_field(*input, "uv")->member_index == 1);

		// Return boundary (sema-resolved): clip placement maps to a built-in
		// position slot, while interpolation remains a varying decoration.
		const StageInterface* varying = find_interface_by(art, "Vertex", StageRole::varying);
		REQUIRE(varying != nullptr);
		const StageIOField* position = find_field(*varying, "position");
		REQUIRE(position->interpolation == InterpolationKind::none);
		REQUIRE(position->placement == StageFieldPlacement::clip_position);
		REQUIRE(position->member_index == 0);
		REQUIRE(find_field(*varying, "uv")->member_index == 1);
		REQUIRE(find_field(*varying, "material")->member_index == 2);
	};

	check_indices(artifact);

	// The member index must survive a serialization round-trip.
	Artifact reloaded;
	DiagnosticEngine diagnostics;
	REQUIRE(read_artifact(artifact.bytes, reloaded, &diagnostics));
	check_indices(reloaded);
}

TEST_CASE("type checker rejects a return type mismatch") {
	CompilerInstance compiler;
	auto artifact = compiler.compile_source(
		"struct Point { vec3 position; };\n"
		"struct Vertex { vec4 position; };\n"
		"fn wrong(Point p) -> Vertex { return p; }\n",
		CompilerInvocation{ .source_name = "ret_struct.rtsl" }
	);
	(void)artifact;
	REQUIRE(compiler.diagnostics().has_error());
	REQUIRE(has_diagnostic_code(compiler, DiagnosticCode::sema_type_mismatch));
}

TEST_CASE("type checker rejects returning a wrongly sized vector") {
	CompilerInstance compiler;
	auto artifact = compiler.compile_source(
		"fn wrong() -> vec4 { vec3 v = vec3(1.0, 2.0, 3.0); return v; }\n",
		CompilerInvocation{ .source_name = "ret_vec.rtsl" }
	);
	(void)artifact;
	REQUIRE(compiler.diagnostics().has_error());
	REQUIRE(has_diagnostic_code(compiler, DiagnosticCode::sema_type_mismatch));
}

TEST_CASE("type checker accepts a return whose type is inferred through a field access") {
	CompilerInstance compiler;
	auto artifact = compiler.compile_source(
		"struct Point { vec3 position; vec2 uv; };\n"
		"fn get_position(Point p) -> vec3 { return p.position; }\n",
		CompilerInvocation{ .source_name = "ret_member.rtsl" }
	);
	REQUIRE_FALSE(has_diagnostic_code(compiler, DiagnosticCode::sema_type_mismatch));
	REQUIRE_FALSE(artifact.bytes.empty());
}

TEST_CASE("type checker rejects unknown struct member access") {
	CompilerInstance compiler;
	auto artifact = compiler.compile_source(
		"struct Point { vec3 position; };\n"
		"fn bad(Point p) -> vec3 { return p.normal; }\n",
		CompilerInvocation{ .source_name = "bad_member.rtsl" }
	);
	(void)artifact;
	REQUIRE(compiler.diagnostics().has_error());
	REQUIRE(has_diagnostic_code(compiler, DiagnosticCode::sema_unknown_member));
}

TEST_CASE("type checker rejects invalid vector swizzle") {
	CompilerInstance compiler;
	auto artifact = compiler.compile_source(
		"fn bad(vec3 v) -> f32 { return v.m; }\n",
		CompilerInvocation{ .source_name = "bad_swizzle.rtsl" }
	);
	(void)artifact;
	REQUIRE(compiler.diagnostics().has_error());
	REQUIRE(has_diagnostic_code(compiler, DiagnosticCode::sema_unknown_member));
}

TEST_CASE("type checker rejects a call with the wrong argument count") {
	CompilerInstance compiler;
	auto artifact = compiler.compile_source(
		"fn add(f32 a, f32 b) -> f32 { return a + b; }\n"
		"fn use() -> f32 { return add(1.0); }\n",
		CompilerInvocation{ .source_name = "arity.rtsl" }
	);
	(void)artifact;
	REQUIRE(has_diagnostic_code(compiler, DiagnosticCode::sema_argument_mismatch));
}

TEST_CASE("type checker rejects a call with an incompatible argument type") {
	CompilerInstance compiler;
	auto artifact = compiler.compile_source(
		"fn add(f32 a, f32 b) -> f32 { return a + b; }\n"
		"fn use(vec3 v) -> f32 { return add(v, 2.0); }\n",
		CompilerInvocation{ .source_name = "argtype.rtsl" }
	);
	(void)artifact;
	REQUIRE(has_diagnostic_code(compiler, DiagnosticCode::sema_argument_mismatch));
}

TEST_CASE("type checker accepts a viable overloaded call") {
	CompilerInstance compiler;
	auto artifact = compiler.compile_source(
		"fn shade(f32 x) -> f32 { return x; }\n"
		"fn shade(vec3 v) -> vec3 { return v; }\n"
		"fn use(vec3 v) -> vec3 { return shade(v); }\n",
		CompilerInvocation{ .source_name = "overload_ok.rtsl" }
	);
	REQUIRE_FALSE(has_diagnostic_code(compiler, DiagnosticCode::sema_argument_mismatch));
	REQUIRE_FALSE(artifact.bytes.empty());
}

TEST_CASE("type checker rejects a non-viable overloaded call") {
	CompilerInstance compiler;
	auto artifact = compiler.compile_source(
		"fn shade(f32 x) -> f32 { return x; }\n"
		"fn shade(vec3 v) -> vec3 { return v; }\n"
		"fn use(mat4 m) -> mat4 { return shade(m); }\n",
		CompilerInvocation{ .source_name = "overload_bad.rtsl" }
	);
	(void)artifact;
	REQUIRE(has_diagnostic_code(compiler, DiagnosticCode::sema_argument_mismatch));
}

TEST_CASE("type checker leaves constructor and builtin calls alone") {
	CompilerInstance compiler;
	auto artifact = compiler.compile_source(
		"struct Point { vec3 position; };\n"
		"struct Vertex { vec4 position; fn Vertex(Point p); };\n"
		"fn Vertex::Vertex(Point p) { position = vec4(p.position, 1.0); }\n"
		"fn build(Point p) -> Vertex { return Vertex(p); }\n",
		CompilerInvocation{ .source_name = "ctor_calls.rtsl" }
	);
	REQUIRE_FALSE(has_diagnostic_code(compiler, DiagnosticCode::sema_argument_mismatch));
	REQUIRE_FALSE(artifact.bytes.empty());
}

TEST_CASE("type checker accepts builtin, resource, and struct type spellings") {
	CompilerInstance compiler;
	auto artifact = compiler.compile_source(
		"struct Point { vec3 position; vec2 uv; };\n"
		"uniform camera {\n"
		"    UniformBuffer data;\n"
		"    Sampler2D albedo;\n"
		"}\n"
		"layout camera::data : mat4;\n"
		"fn use_point(Point p) -> vec4 {\n"
		"    vec3 local = p.position;\n"
		"    return vec4(local, 1.0);\n"
		"}\n",
		CompilerInvocation{ .source_name = "good_types.rtsl" }
	);
	REQUIRE_FALSE(has_diagnostic_code(compiler, DiagnosticCode::sema_unknown_type));
	REQUIRE_FALSE(artifact.bytes.empty());
}
