#include "artifact/artifact.hpp"
#include "driver/compiler.hpp"
#include "frontend/lexer.hpp"
#include "artifact/linker.hpp"
#include "sema/mangler.hpp"
#include "frontend/parser.hpp"
#include "rtsl/sdk.hpp"
#include "rtslc.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>

using namespace rtsl;

namespace {
bool has_diagnostic_code(CompilerInstance& compiler, DiagnosticCode code) {
	for (const auto& diagnostic : compiler.diagnostics().diagnostics()) {
		if (diagnostic.code == static_cast<int>(code)) {
			return true;
		}
	}
	return false;
}

std::span<const std::byte> as_bytes(rtsl_blob blob) {
	return { reinterpret_cast<const std::byte*>(blob.data), blob.size };
}
} // namespace

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
		std::ofstream helper{ helper_path, std::ios::binary };
		helper << "export fn helper() {}\n";
	}

	CompilerInstance compiler;
	Artifact forwarder;
	compiler.compile_source_to(
		forwarder,
		"export import \"helper.rtsl\";\n",
		CompilerInvocation{
			.source_name = (dir / "forwarder.rtsl").string(),
			.import_paths = { dir.string() },
		}
	);
	REQUIRE_FALSE(compiler.diagnostics().has_error());

	const Artifact module = extract_module_interface(forwarder);
	REQUIRE_FALSE(module.bytes.empty());
	REQUIRE(module.module.exports.size() == 1);
	REQUIRE(module.module.exports[0].name == "helper");
}

TEST_CASE("compiler reports source import cycles") {
	const auto dir = std::filesystem::temp_directory_path() / "rtsl-import-cycle-test";
	std::filesystem::create_directories(dir);
	{
		std::ofstream a{ dir / "a.rtsl", std::ios::binary };
		a << "import \"b.rtsl\";\nexport fn a() {}\n";
	}
	{
		std::ofstream b{ dir / "b.rtsl", std::ios::binary };
		b << "import \"a.rtsl\";\nexport fn b() {}\n";
	}

	CompilerInstance compiler;
	auto artifact = compiler.compile_source(
		"import \"a.rtsl\";\nexport fn root() {}\n",
		CompilerInvocation{
			.source_name = (dir / "root.rtsl").string(),
			.import_paths = { dir.string() },
		}
	);
	(void)artifact;
	REQUIRE(compiler.diagnostics().has_error());
}

TEST_CASE("compiler rejects non-v0.1 stage values") {
	CompilerInstance compiler;
	auto artifact = compiler.compile_source(
		"@stage : compute fn compute_entry() {}\n",
		CompilerInvocation{ .source_name = "bad_stage.rtsl" }
	);
	REQUIRE(compiler.diagnostics().has_error());
	REQUIRE(has_diagnostic_code(compiler, DiagnosticCode::sema_invalid_stage));
	REQUIRE(artifact.bytes.empty());
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
	Linker linker{ compiler.diagnostics() };
	REQUIRE(linker.add_artifact(object));
	auto program = linker.link_program();
	REQUIRE_FALSE(compiler.diagnostics().has_error());
	REQUIRE(program.kind == ArtifactKind::program);
	REQUIRE_FALSE(program.bytes.empty());

	Artifact loaded;
	REQUIRE(read_artifact(program.bytes, loaded));
	REQUIRE(loaded.module.entries.size() == 2);
	bool found_vertex_entry = false;
	bool found_fragment_entry = false;
	for (const auto& entry : loaded.module.entries) {
		if (entry.stage == Stage::vertex) {
			REQUIRE(entry.name == "vertex_entry");
			REQUIRE(entry.function != ID<IRInstruction>{});
			found_vertex_entry = true;
		}
		if (entry.stage == Stage::fragment) {
			REQUIRE(entry.name == "fragment_entry");
			REQUIRE(entry.function != ID<IRInstruction>{});
			found_fragment_entry = true;
		}
	}
	REQUIRE(found_vertex_entry);
	REQUIRE(found_fragment_entry);
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
	Linker linker{ compiler.diagnostics() };
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

	Linker linker{ compiler.diagnostics() };
	REQUIRE(linker.add_artifact(object));
	auto program = linker.link_program();
	REQUIRE(has_diagnostic_code(compiler, DiagnosticCode::link_missing_entry));
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

	Linker linker{ compiler.diagnostics() };
	REQUIRE(linker.add_artifact(object));
	auto program = linker.link_program();
	REQUIRE(has_diagnostic_code(compiler, DiagnosticCode::link_missing_stage));
	REQUIRE(program.bytes.empty());
}

TEST_CASE("program link rejects duplicate stage entries") {
	CompilerInstance compiler;
	auto object = compiler.compile_source(
		"struct Point { vec3 position; };\n"
		"struct Vertex { vec4 position; };\n"
		"@stage : vertex fn vertex_a(Point p) -> Vertex : position(clip) { return Vertex(vec4(p.position, 1.0)); }\n"
		"@stage : vertex fn vertex_b(Point p) -> Vertex { return Vertex(vec4(p.position, 1.0)); }\n"
		"@stage : fragment fn fragment_entry(Vertex v) -> vec4 { return vec4(1.0, 0.0, 0.0, 1.0); }\n",
		CompilerInvocation{ .source_name = "duplicate_vertex.rtsl" }
	);
	REQUIRE_FALSE(compiler.diagnostics().has_error());

	Linker linker{ compiler.diagnostics() };
	REQUIRE(linker.add_artifact(object));
	auto program = linker.link_program();
	REQUIRE(has_diagnostic_code(compiler, DiagnosticCode::link_duplicate_stage));
	REQUIRE(program.bytes.empty());
}

TEST_CASE("program link rejects duplicate fragment stage entries") {
	CompilerInstance compiler;
	auto object = compiler.compile_source(
		"struct Point { vec3 position; };\n"
		"struct Vertex { vec4 position; };\n"
		"@stage : vertex fn vertex_entry(Point p) -> Vertex : position(clip) { return Vertex(vec4(p.position, 1.0)); }\n"
		"@stage : fragment fn fragment_a(Vertex v) -> vec4 { return vec4(1.0, 0.0, 0.0, 1.0); }\n"
		"@stage : fragment fn fragment_b(Vertex v) -> vec4 { return vec4(0.0, 1.0, 0.0, 1.0); }\n",
		CompilerInvocation{ .source_name = "duplicate_fragment.rtsl" }
	);
	REQUIRE_FALSE(compiler.diagnostics().has_error());

	Linker linker{ compiler.diagnostics() };
	REQUIRE(linker.add_artifact(object));
	auto program = linker.link_program();
	REQUIRE(has_diagnostic_code(compiler, DiagnosticCode::link_duplicate_stage));
	REQUIRE(program.bytes.empty());
}

TEST_CASE("program link resolves imported helper calls from separate objects") {
	const auto dir = std::filesystem::temp_directory_path() / "rtsl-cross-object-program-test";
	std::filesystem::create_directories(dir);

	CompilerInstance compiler;
	auto helper = compiler.compile_source(
		"export fn tint(vec2 uv) -> vec4 { return vec4(uv, 0.0, 1.0); }\n",
		CompilerInvocation{ .source_name = (dir / "helper.rtsl").string() }
	);
	REQUIRE_FALSE(compiler.diagnostics().has_error());
	const Artifact helper_interface = extract_module_interface(helper);
	{
		std::ofstream sidecar{ dir / "helper.rtslm", std::ios::binary };
		sidecar.write(reinterpret_cast<const char*>(helper_interface.bytes.data()), static_cast<std::streamsize>(helper_interface.bytes.size()));
	}

	auto root = compiler.compile_source(
		"import \"helper.rtsl\";\n"
		"struct Point { vec3 position; vec2 uv; };\n"
		"struct Vertex { vec4 position; vec2 uv; };\n"
		"@stage : vertex fn vertex_entry(Point p) -> Vertex : position(clip), uv(smooth) { return Vertex(vec4(p.position, 1.0), p.uv); }\n"
		"@stage : fragment fn fragment_entry(Vertex v) -> vec4 { return tint(v.uv); }\n",
		CompilerInvocation{
			.source_name = (dir / "root.rtsl").string(),
			.import_paths = { dir.string() },
		}
	);
	REQUIRE_FALSE(compiler.diagnostics().has_error());

	Linker linker{ compiler.diagnostics() };
	REQUIRE(linker.add_artifact(root));
	REQUIRE(linker.add_artifact(helper));
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

	Linker linker{ compiler.diagnostics() };
	REQUIRE(linker.add_artifact(first));
	REQUIRE(linker.add_artifact(second));
	auto library = linker.link_library();
	REQUIRE(has_diagnostic_code(compiler, DiagnosticCode::link_conflict));
	REQUIRE(library.bytes.empty());
}

TEST_CASE("program link rejects unresolved imported calls") {
	const auto dir = std::filesystem::temp_directory_path() / "rtsl-unresolved-import-test";
	std::filesystem::create_directories(dir);
	{
		std::ofstream helper{ dir / "helper.rtsl", std::ios::binary };
		helper << "export fn tint() -> vec4 { return vec4(1.0, 1.0, 1.0, 1.0); }\n";
	}

	CompilerInstance compiler;
	auto object = compiler.compile_source(
		"import \"helper.rtsl\";\n"
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

	Linker linker{ compiler.diagnostics() };
	REQUIRE(linker.add_artifact(object));
	auto program = linker.link_program();
	REQUIRE(has_diagnostic_code(compiler, DiagnosticCode::link_unresolved_call));
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
		std::ofstream sidecar{ dir / "helper.rtslm", std::ios::binary };
		sidecar.write(reinterpret_cast<const char*>(old_interface.bytes.data()), static_cast<std::streamsize>(old_interface.bytes.size()));
	}

	CompilerInstance compiler;
	auto root = compiler.compile_source(
		"import \"helper.rtsl\";\n"
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

	Linker linker{ compiler.diagnostics() };
	REQUIRE(linker.add_artifact(root));
	REQUIRE(linker.add_artifact(changed_helper));
	auto program = linker.link_program();
	REQUIRE(has_diagnostic_code(compiler, DiagnosticCode::link_conflict));
	REQUIRE(program.bytes.empty());
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

	Linker linker{ compiler.diagnostics() };
	REQUIRE(linker.add_artifact(object));
	auto program = linker.link_program();
	REQUIRE_FALSE(compiler.diagnostics().has_error());
	REQUIRE(program.kind == ArtifactKind::program);
	REQUIRE_FALSE(program.bytes.empty());
}

TEST_CASE("C ABI lifetime and errors") {
	REQUIRE(rtslGetVersionMajor() == rtsl::ArtifactVersionMajor);
	REQUIRE(rtslGetVersionMinor() == rtsl::ArtifactVersionMinor);

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
	auto sdk_program = load_program(as_bytes(program_bytes));
	REQUIRE(sdk_program.has_value());
	REQUIRE(sdk_program->entries().size() == 2);
	const EntryPoint* vertex = sdk_program->entry(Stage::vertex);
	const EntryPoint* fragment = sdk_program->entry(Stage::fragment);
	REQUIRE(vertex != nullptr);
	REQUIRE(fragment != nullptr);
	REQUIRE(vertex->name == "vertex_entry");
	REQUIRE(fragment->name == "fragment_entry");
	const ir::Function* fn = sdk_program->find_function(vertex->function);
	REQUIRE(fn != nullptr);
	REQUIRE_FALSE(fn->blocks.empty());
	REQUIRE_FALSE(fn->blocks.front().instructions.empty());

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

	auto sdk_program = load_program(as_bytes(bytes));
	REQUIRE(sdk_program.has_value());
	REQUIRE(sdk_program->resources().size() == 2);
	const auto& first = sdk_program->resources()[0];
	const auto& second = sdk_program->resources()[1];
	REQUIRE(first.name == "albedo.texture");
	REQUIRE(first.kind == ResourceKind::sampled_texture);
	REQUIRE(first.descriptor.set == 0);
	REQUIRE(first.descriptor.binding == 0);
	REQUIRE(second.name == "albedo.tint");
	REQUIRE(second.kind == ResourceKind::uniform_buffer);
	REQUIRE(second.descriptor.set == 0);
	REQUIRE(second.descriptor.binding == 1);

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

TEST_CASE("IR expression diagnostics retain their source location") {
	CompilerInstance compiler;
	auto artifact = compiler.compile_source(
		"fn broken() -> f32 {\n"
		"    return missing;\n"
		"}\n",
		CompilerInvocation{ .source_name = "located_error.rtsl" }
	);
	(void)artifact;

	const Diagnostic* expression_error = nullptr;
	for (const auto& diagnostic : compiler.diagnostics().diagnostics()) {
		if (diagnostic.code == static_cast<int>(DiagnosticCode::ir_expression_error)) {
			expression_error = &diagnostic;
			break;
		}
	}
	REQUIRE(expression_error != nullptr);
	REQUIRE(expression_error->location.line == 2);
	REQUIRE(expression_error->location.column == 12);
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

TEST_CASE("compiler rejects struct-returning vertex stage without boundary tags") {
	CompilerInstance compiler;
	auto artifact = compiler.compile_source(
		"struct Vertex { vec4 position; };\n"
		"@stage : vertex fn vertex_entry() -> Vertex { return Vertex(vec4(0.0, 0.0, 0.0, 1.0)); }\n",
		CompilerInvocation{ .source_name = "struct_stage_return.rtsl" }
	);
	REQUIRE(compiler.diagnostics().has_error());
	REQUIRE(artifact.bytes.empty());
}

TEST_CASE("compiler rejects return boundary fields outside the payload") {
	CompilerInstance compiler;
	auto artifact = compiler.compile_source(
		"struct Vertex { vec4 position; };\n"
		"@stage : vertex fn vertex_entry() -> Vertex : missing(clip) { return Vertex(vec4(0.0, 0.0, 0.0, 1.0)); }\n",
		CompilerInvocation{ .source_name = "bad_boundary_field.rtsl" }
	);
	REQUIRE(compiler.diagnostics().has_error());
	REQUIRE(has_diagnostic_code(compiler, DiagnosticCode::ir_invalid_stage_signature));
	REQUIRE(artifact.bytes.empty());
}

TEST_CASE("compiler rejects fragment input that does not match vertex payload") {
	CompilerInstance compiler;
	auto artifact = compiler.compile_source(
		"struct Point { vec3 position; };\n"
		"struct Vertex { vec4 position; };\n"
		"struct Other { vec4 position; };\n"
		"@stage : vertex fn vertex_entry(Point p) -> Vertex : position(clip) { return Vertex(vec4(p.position, 1.0)); }\n"
		"@stage : fragment fn fragment_entry(Other v) -> vec4 { return vec4(v.position.xyz, 1.0); }\n",
		CompilerInvocation{ .source_name = "bad_fragment_input.rtsl" }
	);
	REQUIRE(compiler.diagnostics().has_error());
	REQUIRE(has_diagnostic_code(compiler, DiagnosticCode::ir_invalid_stage_signature));
	REQUIRE(artifact.bytes.empty());
}

namespace {
bool module_has_op(const Artifact& artifact, IROp op) {
	for (const auto& inst : artifact.module.type_constant_pool) {
		if (inst.op == op) {
			return true;
		}
	}
	for (const auto& fn : artifact.module.functions) {
		for (const auto& inst : fn.body) {
			if (inst.op == op) {
				return true;
			}
		}
	}
	return false;
}
} // namespace

TEST_CASE("compiler lowers multi-component swizzles to VectorShuffle") {
	// A partial or reordering swizzle (`.xy`, `.zyx`) must select the named
	// components, not silently pass the whole vector through.
	CompilerInstance compiler;
	auto artifact = compiler.compile_source(
		"fn shuffle(vec3 v) -> vec3 {\n"
		"    vec2 a = v.xy;\n"
		"    return v.zyx;\n"
		"}\n",
		CompilerInvocation{ .source_name = "swizzle.rtsl" }
	);
	REQUIRE_FALSE(compiler.diagnostics().has_error());
	Artifact loaded;
	REQUIRE(read_artifact(artifact.bytes, loaded));
	REQUIRE(module_has_op(loaded, IROp::VectorShuffle));
}

TEST_CASE("compiler lowers comparison and logical operators to boolean ops") {
	// Relational and logical operators must produce real boolean values so a
	// condition is not a null branch operand.
	CompilerInstance compiler;
	auto artifact = compiler.compile_source(
		"fn pick(vec3 v) -> vec4 {\n"
		"    if (v.x < 0.5 && v.y > 0.2) { return vec4(1.0, 0.0, 0.0, 1.0); }\n"
		"    return vec4(0.0, 0.0, 0.0, 1.0);\n"
		"}\n",
		CompilerInvocation{ .source_name = "compare.rtsl" }
	);
	REQUIRE_FALSE(compiler.diagnostics().has_error());
	Artifact loaded;
	REQUIRE(read_artifact(artifact.bytes, loaded));
	REQUIRE(module_has_op(loaded, IROp::FOrdLess));
	REQUIRE(module_has_op(loaded, IROp::FOrdGreater));
	REQUIRE(module_has_op(loaded, IROp::LogicalAnd));
}

TEST_CASE("compiler lowers boolean literals to ConstantBool") {
	CompilerInstance compiler;
	auto artifact = compiler.compile_source(
		"fn flag() -> bool { return true; }\n",
		CompilerInvocation{ .source_name = "bool_literal.rtsl" }
	);
	REQUIRE_FALSE(compiler.diagnostics().has_error());
	Artifact loaded;
	REQUIRE(read_artifact(artifact.bytes, loaded));
	REQUIRE(module_has_op(loaded, IROp::ConstantBool));
}

TEST_CASE("compiler builds stage interfaces from the return boundary") {
	// The return-boundary grammar is how a stage declares its interface. The
	// compiler resolves the authored varying and derives the vertex input and
	// fragment color output — RTSL has no input/output/varying globals.
	CompilerInstance compiler;
	auto artifact = compiler.compile_source(
		"struct Point { vec3 position; vec2 uv; };\n"
		"struct Vertex { vec4 position; vec2 uv; };\n"
		"@stage : vertex fn vertex_entry(Point p) -> Vertex : position(clip), uv(smooth) { return Vertex(vec4(p.position, 1.0), p.uv); }\n"
		"@stage : fragment fn fragment_entry(Vertex v) -> vec4 { return vec4(v.uv, 0.0, 1.0); }\n",
		CompilerInvocation{ .source_name = "graphics.rtsl" }
	);
	REQUIRE_FALSE(compiler.diagnostics().has_error());
	Artifact loaded;
	REQUIRE(read_artifact(artifact.bytes, loaded));

	const EntryPoint* vertex = nullptr;
	const EntryPoint* fragment = nullptr;
	for (const auto& entry : loaded.module.entries) {
		if (entry.stage == Stage::vertex) vertex = &entry;
		if (entry.stage == Stage::fragment) fragment = &entry;
	}
	REQUIRE(vertex != nullptr);
	REQUIRE(fragment != nullptr);
	REQUIRE(vertex->input.has_value());
	REQUIRE(vertex->input->elements.size() == 2);
	REQUIRE(vertex->output.has_value());
	REQUIRE(vertex->output->elements.size() == 2);
	REQUIRE(vertex->output->elements[0].name == "position");
	REQUIRE(vertex->output->elements[0].builtin == Builtin::position);
	REQUIRE_FALSE(vertex->output->elements[0].location.has_value());
	REQUIRE(vertex->output->elements[1].name == "uv");
	REQUIRE(vertex->output->elements[1].interpolation == Interpolation::smooth);
	REQUIRE(vertex->output->elements[1].location == 0);
	REQUIRE(fragment->input.has_value());
	REQUIRE(fragment->input->elements.size() == 1);
	REQUIRE(fragment->output.has_value());
	REQUIRE(fragment->output->elements.size() == 1);
	REQUIRE(fragment->output->elements[0].name == "color");
	REQUIRE(fragment->output->elements[0].location == 0);
}

TEST_CASE("compiler rejects an unknown return-boundary tag") {
	CompilerInstance compiler;
	auto artifact = compiler.compile_source(
		"struct Vertex { vec4 position; };\n"
		"@stage : vertex fn vertex_entry() -> Vertex : position(nonsense) { return Vertex(vec4(0.0, 0.0, 0.0, 1.0)); }\n",
		CompilerInvocation{ .source_name = "bad_tag.rtsl" }
	);
	(void)artifact;
	REQUIRE(has_diagnostic_code(compiler, DiagnosticCode::sema_unknown_name));
}

TEST_CASE("C ABI reports malformed artifact load failures distinctly") {
	rtsl_context ctx = rtslCreateContext();
	REQUIRE(ctx != nullptr);
	const uint8_t bytes[] = { 'R', 'T', 'S', 'L' };
	rtsl_module module = rtslLoadModule(ctx, bytes, sizeof(bytes));
	REQUIRE(module == nullptr);
	const rtsl_result result = rtslCtxGetResult(ctx);
	REQUIRE(result.code == RTSL_ERROR_ARTIFACT_FAILED);
	rtslDestroyContext(ctx);
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
	REQUIRE(artifact.module.resources.size() == 7);
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

TEST_CASE("type checker resolves fields of inline buffer layouts") {
	CompilerInstance compiler;
	auto artifact = compiler.compile_source(
		"uniform { UniformBuffer scene; }\n"
		"layout scene : struct { mat4 transform; f32 exposure; };\n"
		"fn read_transform() -> mat4 { return scene.transform; }\n"
		"fn read_exposure() -> f32 { return scene.exposure; }\n",
		CompilerInvocation{ .source_name = "inline_uniform_layout.rtsl" }
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

TEST_CASE("type checker rejects auto as a v0.1 type spelling") {
	CompilerInstance compiler;
	auto artifact = compiler.compile_source(
		"fn bad(auto value) {}\n",
		CompilerInvocation{ .source_name = "bad_auto.rtsl" }
	);
	(void)artifact;
	REQUIRE(compiler.diagnostics().has_error());
	REQUIRE(has_diagnostic_code(compiler, DiagnosticCode::sema_unknown_type));
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

TEST_CASE("type checker rejects invalid sample arguments") {
	CompilerInstance compiler;
	auto artifact = compiler.compile_source(
		"fn bad(vec2 uv) -> vec4 {\n"
		"    return sample(uv, uv);\n"
		"}\n",
		CompilerInvocation{ .source_name = "bad_sample.rtsl" }
	);
	REQUIRE(compiler.diagnostics().has_error());
	REQUIRE(has_diagnostic_code(compiler, DiagnosticCode::sema_argument_mismatch));
	REQUIRE(artifact.bytes.empty());
}

TEST_CASE("runtime-array layouts require storage buffers") {
	CompilerInstance compiler;
	auto artifact = compiler.compile_source(
		"uniform { UniformBuffer values; }\n"
		"layout values : vec4[];\n",
		CompilerInvocation{ .source_name = "bad_runtime_array_resource.rtsl" }
	);
	REQUIRE(compiler.diagnostics().has_error());
	REQUIRE(has_diagnostic_code(compiler, DiagnosticCode::layout_invalid_uniform_kind));
	REQUIRE(artifact.bytes.empty());
}

TEST_CASE("runtime-array indexing requires an integer index") {
	CompilerInstance compiler;
	auto artifact = compiler.compile_source(
		"uniform { readonly StorageBuffer values; }\n"
		"layout values : vec4[];\n"
		"fn bad(f32 index) -> vec4 { return values[index]; }\n",
		CompilerInvocation{ .source_name = "bad_runtime_array_index.rtsl" }
	);
	REQUIRE(compiler.diagnostics().has_error());
	REQUIRE(has_diagnostic_code(compiler, DiagnosticCode::sema_type_mismatch));
	REQUIRE(artifact.bytes.empty());
}
