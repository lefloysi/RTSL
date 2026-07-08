#include "artifact/artifact.hpp"
#include "driver/compiler.hpp"
#include "frontend/lexer.hpp"
#include "artifact/linker.hpp"
#include "sema/mangler.hpp"
#include "frontend/parser.hpp"
#include "rtsl.h"

#include <catch2/catch_test_macros.hpp>

#include <cstring>

using namespace rtsl;
TEST_CASE("compiler honors basic preprocessor blocks") {
	CompilerInstance compiler;
	const char* source = R"(
#define ENABLED
#ifdef ENABLED
export fn main() {}
#endif
)";
	auto artifact = compiler.compile_source(source, CompilerInvocation{ .source_name = "pp.rtsl" });
	REQUIRE_FALSE(compiler.diagnostics().has_error());
	REQUIRE(artifact.kind == ArtifactKind::object);
	REQUIRE_FALSE(artifact.bytes.empty());
}

TEST_CASE("compiler emits object") {
	CompilerInstance compiler;
	auto artifact = compiler.compile_source(
		"struct Point { vec3 position; };\n"
		"struct Vertex { vec3 position; };\n"
		"export fn main(Point p) -> Vertex { return Vertex(p.position); }\n",
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
	auto object = compiler.compile_source("export fn main() {}", CompilerInvocation{ .source_name = "test.rtsl" });
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
	const char* source = "export fn main() {}";
	rtsl_module object = rtslCompileSource(ctx, source, std::strlen(source), "abi.rtsl");
	REQUIRE(object);
	REQUIRE(rtslModuleGetBytecode(object).size > 0);

	rtsl_linker linker = rtslCreateLinker(ctx);
	REQUIRE(linker);
	REQUIRE(rtslLinkerAddModule(linker, object));
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
		"fn main(Point p) -> Vertex {\n"
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
		"fn main() {\n"
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
		"fn main(f32 a, f32 b) {\n"
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
		"fn main(Point p) -> Vertex {\n"
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
		"fn main(Point p) -> Vertex {\n"
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
		"export fn main() -> vec4 {\n"
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
		"export fn main(vec2 uv) -> vec4 {\n"
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
		"export fn main(vec2 uv) -> vec4 {\n"
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
		"export fn main(vec2 uv) -> vec4 {\n"
		"    return sample(texture, uv) * tint;\n"
		"}\n",
		CompilerInvocation{ .source_name = "missing_using_uniform_scope.rtsl" }
	);
	(void)artifact;
	REQUIRE(compiler.diagnostics().has_error());
	REQUIRE(artifact.bytes.empty());
}
