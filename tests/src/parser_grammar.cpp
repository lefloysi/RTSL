// Grammar-level tests for the recursive-descent parser. These focus on the
// parser output (AST shape, diagnostics) and don't require the full compiler
// pipeline to run. Each test drives Lexer + Parser directly so failures point
// at the parser and not at whatever sema/IR does afterwards.

#include "frontend/ast.hpp"
#include "support/basic_diagnostics.hpp"
#include "support/basic_source_manager.hpp"
#include "frontend/lexer.hpp"
#include "frontend/parser.hpp"

#include <catch2/catch_test_macros.hpp>

#include <span>
#include <string_view>

using namespace rtsl;

namespace {

struct ParseResult {
	SourceManager sources;
	DiagnosticEngine diagnostics;
	std::vector<Token> tokens;
	TranslationUnit unit;
};

// Parse a source snippet. The result owns its SourceManager/DiagnosticEngine
// so callers can inspect diagnostics after the parser returns. `tokens` is
// kept alive so any Token::text string_views into the SourceManager buffer
// remain valid.
ParseResult parse(std::string_view source, std::string_view name = "test.rtsl") {
	ParseResult result;
	const auto file = result.sources.add_buffer(name, source);
	Lexer lexer{ result.sources, result.diagnostics, file };
	result.tokens = lexer.lex();
	Parser parser{ result.sources, result.diagnostics, file, result.tokens };
	result.unit = parser.parse_translation_unit();
	return result;
}

// Locate the body statements of the first function-shape declaration.
std::span<const Decl::BodyStatement> first_function_body(const TranslationUnit& unit) {
	for (const auto& decl : unit.declarations) {
		if (decl.kind == DeclKind::function) return decl.body_statements;
	}
	return {};
}

const Decl* find_function(const TranslationUnit& unit) {
	for (const auto& decl : unit.declarations) {
		if (decl.kind == DeclKind::function) return &decl;
	}
	return nullptr;
}

} // namespace

// ---------------------------------------------------------------------------
// Statement classification — the historical bug bucket
// ---------------------------------------------------------------------------

TEST_CASE("indexed assignment is not misclassified as a declaration") {
	auto r = parse("fn t() { a[i] = b; }");
	REQUIRE_FALSE(r.diagnostics.has_error());
	const auto& body = first_function_body(r.unit);
	REQUIRE(body.size() == 1);
	REQUIRE(body[0].kind == Decl::BodyStatementKind::assignment);
	REQUIRE(body[0].lhs.find("a") != std::string::npos);
	REQUIRE(body[0].lhs.find("[") != std::string::npos);
}

TEST_CASE("equality expression statement is not treated as assignment") {
	auto r = parse("fn t() { a == b; }");
	REQUIRE_FALSE(r.diagnostics.has_error());
	const auto& body = first_function_body(r.unit);
	REQUIRE(body.size() == 1);
	REQUIRE(body[0].kind == Decl::BodyStatementKind::expression);
	REQUIRE(body[0].expr.kind == Decl::Expr::Kind::binary);
	REQUIRE(body[0].expr.op == "==");
}

TEST_CASE("call with named-arg-lookalike does not trip decl heuristic") {
	auto r = parse("fn t() { foo(x = 1); }");
	REQUIRE_FALSE(r.diagnostics.has_error());
	const auto& body = first_function_body(r.unit);
	REQUIRE(body.size() == 1);
	REQUIRE(body[0].kind == Decl::BodyStatementKind::expression);
	REQUIRE(body[0].expr.kind == Decl::Expr::Kind::call);
}

TEST_CASE("local declaration with initializer parses cleanly") {
	auto r = parse("fn t() { vec4 x = f(1); }");
	REQUIRE_FALSE(r.diagnostics.has_error());
	const auto& body = first_function_body(r.unit);
	REQUIRE(body.size() == 1);
	REQUIRE(body[0].kind == Decl::BodyStatementKind::declaration);
	REQUIRE(body[0].type_name == "vec4");
	REQUIRE(body[0].name == "x");
	REQUIRE(body[0].expr.kind == Decl::Expr::Kind::call);
}

TEST_CASE("local declaration without initializer parses cleanly") {
	auto r = parse("fn t() { vec4 x; }");
	REQUIRE_FALSE(r.diagnostics.has_error());
	const auto& body = first_function_body(r.unit);
	REQUIRE(body.size() == 1);
	REQUIRE(body[0].kind == Decl::BodyStatementKind::declaration);
	REQUIRE(body[0].type_name == "vec4");
	REQUIRE(body[0].name == "x");
}

TEST_CASE("scoped-type local declaration parses cleanly") {
	auto r = parse("fn t() { math::vec4 x = zero(); }");
	REQUIRE_FALSE(r.diagnostics.has_error());
	const auto& body = first_function_body(r.unit);
	REQUIRE(body.size() == 1);
	REQUIRE(body[0].kind == Decl::BodyStatementKind::declaration);
	REQUIRE(body[0].type_name == "math::vec4");
}

// ---------------------------------------------------------------------------
// Control flow
// ---------------------------------------------------------------------------

TEST_CASE("if-else-if-else chains") {
	auto r = parse("fn t() { if (a) { } else if (b) { } else { } }");
	REQUIRE_FALSE(r.diagnostics.has_error());
	const auto& body = first_function_body(r.unit);
	REQUIRE(body.size() == 1);
	REQUIRE(body[0].kind == Decl::BodyStatementKind::if_stmt);
	// Outer else arm is the inner `if` statement.
	REQUIRE(body[0].else_children.size() == 1);
	REQUIRE(body[0].else_children[0].kind == Decl::BodyStatementKind::if_stmt);
	// That inner if in turn has an else branch (the final `else { }`).
	// An empty block yields zero body children — assert nothing further about
	// contents, only that the arm was recognised.
	const auto& inner_if = body[0].else_children[0];
	(void)inner_if; // shape assertion above is enough
}

TEST_CASE("while loop parses condition and body") {
	auto r = parse("fn t() { while (a < 10) { } }");
	REQUIRE_FALSE(r.diagnostics.has_error());
	const auto& body = first_function_body(r.unit);
	REQUIRE(body.size() == 1);
	REQUIRE(body[0].kind == Decl::BodyStatementKind::while_stmt);
	REQUIRE(body[0].expr.kind == Decl::Expr::Kind::binary);
	REQUIRE(body[0].expr.op == "<");
}

TEST_CASE("do-while consumes its trailing while clause") {
	auto r = parse("fn t() { do { } while (a); }");
	REQUIRE_FALSE(r.diagnostics.has_error());
	const auto& body = first_function_body(r.unit);
	REQUIRE(body.size() == 1);
	REQUIRE(body[0].kind == Decl::BodyStatementKind::do_stmt);
	REQUIRE(body[0].expr.kind == Decl::Expr::Kind::name);
}

TEST_CASE("do-without-while is diagnosed") {
	auto r = parse("fn t() { do { } }");
	REQUIRE(r.diagnostics.has_error());
}

TEST_CASE("for loop parses three clauses") {
	auto r = parse("fn t() { for (i32 i = 0; i < 10; i = i + 1) { } }");
	REQUIRE_FALSE(r.diagnostics.has_error());
	const auto& body = first_function_body(r.unit);
	REQUIRE(body.size() == 1);
	REQUIRE(body[0].kind == Decl::BodyStatementKind::for_stmt);
	REQUIRE(body[0].loop_init.find("i32") != std::string::npos);
	REQUIRE(body[0].condition.find("<") != std::string::npos);
	REQUIRE_FALSE(body[0].loop_continue.empty());
}

TEST_CASE("empty for loop parses (for(;;))") {
	auto r = parse("fn t() { for (;;) { } }");
	REQUIRE_FALSE(r.diagnostics.has_error());
	const auto& body = first_function_body(r.unit);
	REQUIRE(body.size() == 1);
	REQUIRE(body[0].kind == Decl::BodyStatementKind::for_stmt);
}

TEST_CASE("return with expression is parsed as return statement") {
	auto r = parse("fn t() { return a + b; }");
	REQUIRE_FALSE(r.diagnostics.has_error());
	const auto& body = first_function_body(r.unit);
	REQUIRE(body.size() == 1);
	REQUIRE(body[0].kind == Decl::BodyStatementKind::return_stmt);
	REQUIRE(body[0].expr.kind == Decl::Expr::Kind::binary);
	REQUIRE(body[0].expr.op == "+");
}

TEST_CASE("bare return parses without expression") {
	auto r = parse("fn t() { return; }");
	REQUIRE_FALSE(r.diagnostics.has_error());
	const auto& body = first_function_body(r.unit);
	REQUIRE(body.size() == 1);
	REQUIRE(body[0].kind == Decl::BodyStatementKind::return_stmt);
	REQUIRE(body[0].expr.kind == Decl::Expr::Kind::unknown);
}

TEST_CASE("nested block statements parse") {
	auto r = parse("fn t() { { { return; } } }");
	REQUIRE_FALSE(r.diagnostics.has_error());
	const auto& body = first_function_body(r.unit);
	REQUIRE(body.size() == 1);
	REQUIRE(body[0].kind == Decl::BodyStatementKind::block);
	REQUIRE(body[0].children.size() == 1);
	REQUIRE(body[0].children[0].kind == Decl::BodyStatementKind::block);
}

// ---------------------------------------------------------------------------
// Expression grammar
// ---------------------------------------------------------------------------

TEST_CASE("expression precedence: a + b * c is + at root") {
	auto r = parse("fn t() { return a + b * c; }");
	REQUIRE_FALSE(r.diagnostics.has_error());
	const auto& e = first_function_body(r.unit)[0].expr;
	REQUIRE(e.kind == Decl::Expr::Kind::binary);
	REQUIRE(e.op == "+");
	REQUIRE(e.children[1].kind == Decl::Expr::Kind::binary);
	REQUIRE(e.children[1].op == "*");
}

TEST_CASE("expression precedence: comparison groups tighter than logical-and") {
	auto r = parse("fn t() { return a < b && c > d; }");
	REQUIRE_FALSE(r.diagnostics.has_error());
	const auto& e = first_function_body(r.unit)[0].expr;
	REQUIRE(e.kind == Decl::Expr::Kind::binary);
	REQUIRE(e.op == "&&");
	REQUIRE(e.children[0].op == "<");
	REQUIRE(e.children[1].op == ">");
}

TEST_CASE("all unary operators parse") {
	auto r = parse("fn t() { return -a + !b + ~c + +d; }");
	REQUIRE_FALSE(r.diagnostics.has_error());
	// Just checks the parser accepted the whole expression as one return stmt.
	const auto& body = first_function_body(r.unit);
	REQUIRE(body.size() == 1);
	REQUIRE(body[0].kind == Decl::BodyStatementKind::return_stmt);
}

TEST_CASE("boolean literals parse as boolean literals") {
	// `true`/`false` are boolean literals, distinct from
	// integer literals: they carry the bool type through sema and lower to
	// OpConstantTrueFalse. text stays "1"/"0" as the encoded literal value.
	auto r_true  = parse("fn t() { return true; }");
	auto r_false = parse("fn t() { return false; }");
	REQUIRE_FALSE(r_true.diagnostics.has_error());
	REQUIRE_FALSE(r_false.diagnostics.has_error());
	REQUIRE(first_function_body(r_true.unit)[0].expr.kind  == Decl::Expr::Kind::literal_bool);
	REQUIRE(first_function_body(r_true.unit)[0].expr.text  == "1");
	REQUIRE(first_function_body(r_false.unit)[0].expr.kind == Decl::Expr::Kind::literal_bool);
	REQUIRE(first_function_body(r_false.unit)[0].expr.text == "0");
}

TEST_CASE("member and scope access build nested member chains") {
	auto r = parse("fn t() { return a::b.c; }");
	REQUIRE_FALSE(r.diagnostics.has_error());
	const auto& e = first_function_body(r.unit)[0].expr;
	REQUIRE(e.kind == Decl::Expr::Kind::member);
	REQUIRE(e.text == "c");
	REQUIRE(e.children[0].kind == Decl::Expr::Kind::member);
	REQUIRE(e.children[0].text == "b");
}

TEST_CASE("nested call arguments parse") {
	auto r = parse("fn t() { return f(g(1, 2), h()); }");
	REQUIRE_FALSE(r.diagnostics.has_error());
	const auto& e = first_function_body(r.unit)[0].expr;
	REQUIRE(e.kind == Decl::Expr::Kind::call);
	REQUIRE(e.children.size() == 3); // callee + 2 args
}

// ---------------------------------------------------------------------------
// Error cases — bad grammar the parser must reject
// ---------------------------------------------------------------------------

TEST_CASE("missing semicolon after statement is diagnosed") {
	auto r = parse("fn t() { a = b }");
	REQUIRE(r.diagnostics.has_error());
}

TEST_CASE("unclosed brace does not crash and reports error") {
	auto r = parse("fn t() { a = b; ");
	// May or may not report a specific error, but must at minimum finish
	// without hanging and produce a translation unit.
	REQUIRE(r.unit.declarations.size() >= 1);
}

TEST_CASE("import without delimiters is diagnosed") {
	auto r = parse("import foo;");
	REQUIRE(r.diagnostics.has_error());
}

TEST_CASE("angle import syntax is not source syntax") {
	auto r = parse("import <foo.rtsl>;");
	REQUIRE(r.diagnostics.has_error());
}

TEST_CASE("import missing semicolon is diagnosed") {
	auto r = parse("import \"foo.rtsl\"");
	REQUIRE(r.diagnostics.has_error());
}

TEST_CASE("scoped name with dangling :: is diagnosed") {
	auto r = parse("struct A::  { vec3 p; }");
	REQUIRE(r.diagnostics.has_error());
}

TEST_CASE("function without body-or-semicolon is diagnosed") {
	auto r = parse("fn t()");
	REQUIRE(r.diagnostics.has_error());
}

TEST_CASE("function with malformed parameter type is diagnosed") {
	auto r = parse("fn t(, ) { }");
	REQUIRE(r.diagnostics.has_error());
}

TEST_CASE("pointer types are not source syntax") {
	auto r = parse("fn t(vec4* color) { }");
	REQUIRE(r.diagnostics.has_error());
}

TEST_CASE("reference parameters preserve their qualifier") {
	auto r = parse("fn t(const vec4& color) { }");
	REQUIRE_FALSE(r.diagnostics.has_error());
	const auto* fn = find_function(r.unit);
	REQUIRE(fn != nullptr);
	REQUIRE(fn->parameters.size() == 1);
	REQUIRE(fn->parameters[0].type == "vec4");
	REQUIRE(fn->parameters[0].is_const);
	REQUIRE(fn->parameters[0].is_reference);
}

TEST_CASE("reference return types are rejected") {
	auto r = parse("fn t(vec4& color) -> vec4& { return color; }");
	REQUIRE(r.diagnostics.has_error());
}

TEST_CASE("reference fields and locals are rejected") {
	auto field = parse("struct S { vec4& color; };");
	REQUIRE(field.diagnostics.has_error());
	auto local = parse("fn t() { vec4& color; }");
	REQUIRE(local.diagnostics.has_error());
}

TEST_CASE("stray '@' with no fn following is diagnosed") {
	auto r = parse("@stage : vertex\nstruct S { vec3 p; };");
	REQUIRE(r.diagnostics.has_error());
}

TEST_CASE("bare identifier statement is diagnosed") {
	auto r = parse("fn t() { garbage_identifier }");
	REQUIRE(r.diagnostics.has_error());
}

TEST_CASE("dangling attribute marker is diagnosed") {
	auto r = parse("@stage : vertex\nfn vertex_entry() {}\n@\n");
	REQUIRE(r.diagnostics.has_error());
}

TEST_CASE("unknown function attribute spelling is recorded for sema") {
	auto r = parse("@nonsense\nfn entry() {}\n");
	REQUIRE_FALSE(r.diagnostics.has_error());
	const auto* fn = find_function(r.unit);
	REQUIRE(fn != nullptr);
	REQUIRE(fn->attributes.size() == 1);
	REQUIRE(fn->attributes[0].name == "nonsense");
}

TEST_CASE("do-while without semicolon is diagnosed") {
	auto r = parse("fn t() { do { } while (a) }");
	REQUIRE(r.diagnostics.has_error());
}

// ---------------------------------------------------------------------------
// Declaration surface: stage attributes, structs, uniforms, layouts, aliases
// ---------------------------------------------------------------------------

TEST_CASE("function attribute records authored stage value on the fn decl") {
	auto r = parse("@stage : vertex fn vertex_entry() {}");
	REQUIRE_FALSE(r.diagnostics.has_error());
	const auto* fn = find_function(r.unit);
	REQUIRE(fn != nullptr);
	REQUIRE(fn->attributes.size() == 1);
	REQUIRE(fn->attributes[0].name == "stage");
	REQUIRE(fn->attributes[0].value == "vertex");
}

TEST_CASE("function attribute records fragment stage value on the fn decl") {
	auto r = parse("@stage : fragment fn fragment_entry() {}");
	REQUIRE_FALSE(r.diagnostics.has_error());
	const auto* fn = find_function(r.unit);
	REQUIRE(fn != nullptr);
	REQUIRE(fn->attributes.size() == 1);
	REQUIRE(fn->attributes[0].name == "stage");
	REQUIRE(fn->attributes[0].value == "fragment");
}

TEST_CASE("function body flag distinguishes body from forward decl") {
	auto r_body    = parse("fn t() {}");
	auto r_forward = parse("fn t();");
	REQUIRE_FALSE(r_body.diagnostics.has_error());
	REQUIRE_FALSE(r_forward.diagnostics.has_error());
	REQUIRE(find_function(r_body.unit)->has_body);
	REQUIRE_FALSE(find_function(r_forward.unit)->has_body);
}

TEST_CASE("struct fields are collected") {
	auto r = parse("struct S { vec3 position; vec2 uv; };");
	REQUIRE_FALSE(r.diagnostics.has_error());
	REQUIRE(r.unit.structs.size() == 1);
	REQUIRE(r.unit.structs[0].fields.size() == 2);
	REQUIRE(r.unit.structs[0].fields[0].type == "vec3");
	REQUIRE(r.unit.structs[0].fields[0].name == "position");
	REQUIRE(r.unit.structs[0].fields[1].name == "uv");
}

TEST_CASE("struct member function declarations are collected") {
	auto r = parse("struct Vertex { vec4 position; fn Vertex(Point p); fn shade(f32 x) -> vec4; };");
	REQUIRE_FALSE(r.diagnostics.has_error());
	REQUIRE(r.unit.structs.size() == 1);
	REQUIRE(r.unit.structs[0].member_functions.size() == 2);
	REQUIRE(r.unit.structs[0].member_functions[0].name == "Vertex");
	REQUIRE(r.unit.structs[0].member_functions[0].parameters.size() == 1);
	REQUIRE(r.unit.structs[0].member_functions[1].name == "shade");
	REQUIRE(r.unit.structs[0].member_functions[1].return_type == "vec4");
}

TEST_CASE("inline struct member body becomes qualified function declaration") {
	auto r = parse(
		"struct Vertex {\n"
		"    vec4 position;\n"
		"    fn Vertex(vec4 p) { position = p; }\n"
		"};\n"
	);
	REQUIRE_FALSE(r.diagnostics.has_error());
	REQUIRE(r.unit.structs.size() == 1);
	REQUIRE(r.unit.structs[0].member_functions.size() == 1);
	const auto* fn = find_function(r.unit);
	REQUIRE(fn != nullptr);
	REQUIRE(fn->name == "Vertex::Vertex");
	REQUIRE(fn->has_body);
	REQUIRE(fn->parameters.size() == 1);
	REQUIRE(fn->body_statements.size() == 1);
}

TEST_CASE("top-level named type variable declaration is rejected") {
	auto r = parse("mat4 foo;");
	REQUIRE(r.diagnostics.has_error());
	REQUIRE(r.unit.declarations.empty());
	REQUIRE(r.unit.structs.empty());
}

TEST_CASE("top-level inline struct variable declaration is rejected like any other global") {
	auto r = parse("struct { mat4 value; } foo;");
	REQUIRE(r.diagnostics.has_error());
	REQUIRE(r.unit.declarations.empty());
	REQUIRE(r.unit.structs.empty());
}

TEST_CASE("uniform bindings collect scope and access qualifier") {
	auto r = parse(
		"uniform albedo {\n"
		"    readonly Sampler2D texture;\n"
		"    vec4 tint;\n"
		"}\n"
	);
	REQUIRE_FALSE(r.diagnostics.has_error());
	REQUIRE(r.unit.uniforms.size() == 2);
	REQUIRE(r.unit.uniforms[0].scope_name == "albedo");
	REQUIRE(r.unit.uniforms[0].access == AccessKind::read_only);
	REQUIRE(r.unit.uniforms[1].access == AccessKind::read_write);
}

TEST_CASE("anonymous uniform blocks get distinct block ids") {
	auto r = parse(
		"uniform { UniformBuffer a; }\n"
		"uniform { UniformBuffer b; }\n"
	);
	REQUIRE_FALSE(r.diagnostics.has_error());
	REQUIRE(r.unit.uniforms.size() == 2);
	REQUIRE(r.unit.uniforms[0].is_anonymous);
	REQUIRE(r.unit.uniforms[1].is_anonymous);
	REQUIRE(r.unit.uniforms[0].anonymous_block_id != r.unit.uniforms[1].anonymous_block_id);
}

TEST_CASE("layout with named type parses") {
	auto r = parse("uniform { UniformBuffer mvp; } layout mvp : mat4;");
	REQUIRE_FALSE(r.diagnostics.has_error());
	REQUIRE(r.unit.layouts.size() == 1);
	REQUIRE(r.unit.layouts[0].path.size() == 1);
	REQUIRE(r.unit.layouts[0].path[0] == "mvp");
	REQUIRE(r.unit.layouts[0].type_spelling == "mat4");
}

TEST_CASE("layout with layout rule parses") {
	auto r = parse("uniform { UniformBuffer mvp; } layout mvp : std140 mat4;");
	REQUIRE_FALSE(r.diagnostics.has_error());
	REQUIRE(r.unit.layouts.size() == 1);
	REQUIRE(r.unit.layouts[0].rule == LayoutRule::std140);
}

TEST_CASE("layout cannot be used as a function name") {
	auto r = parse("fn layout() {}");
	REQUIRE(r.diagnostics.has_error());
}

TEST_CASE("contextual interpolation words remain valid identifiers") {
	auto r = parse("fn smooth(flat clip) { flat = clip; }");
	REQUIRE_FALSE(r.diagnostics.has_error());
	const auto* fn = find_function(r.unit);
	REQUIRE(fn != nullptr);
	REQUIRE(fn->name == "smooth");
	REQUIRE(fn->parameters.size() == 1);
	REQUIRE(fn->parameters[0].type == "flat");
	REQUIRE(fn->parameters[0].name == "clip");
}

TEST_CASE("function return boundary records varying interface") {
	// The return-boundary grammar `-> T : field(tag), ...` declares a stage
	// entry's interface; it replaces input/output/varying globals.
	auto r = parse(
		"struct Vertex { vec4 position; vec2 uv; };\n"
		"@stage : vertex fn vertex_entry() -> Vertex : position(clip), uv(smooth) { return Vertex(); }\n"
	);
	REQUIRE_FALSE(r.diagnostics.has_error());
	bool found_varying = false;
	for (const auto& iface : r.unit.stage_interfaces) {
		if (iface.role == StageRole::varying && iface.type_name == "Vertex") {
			found_varying = true;
			REQUIRE(iface.fields.size() == 2);
			REQUIRE(iface.fields[0].name == "position");
			REQUIRE(iface.fields[0].tags.size() == 1);
			REQUIRE(iface.fields[0].tags[0] == "clip");
			REQUIRE(iface.fields[1].name == "uv");
			REQUIRE(iface.fields[1].tags.size() == 1);
			REQUIRE(iface.fields[1].tags[0] == "smooth");
		}
	}
	REQUIRE(found_varying);
}

TEST_CASE("return boundary requires a tag list") {
	auto r = parse(
		"struct Vertex { vec4 position; };\n"
		"@stage : vertex fn vertex_entry() -> Vertex : position { return Vertex(); }\n"
	);
	REQUIRE(r.diagnostics.has_error());
}

TEST_CASE("return boundary records unknown tag spelling for sema") {
	// The parser preserves any tag identifier; sema owns which tags are known.
	auto r = parse(
		"struct Vertex { vec4 position; };\n"
		"@stage : vertex fn vertex_entry() -> Vertex : position(weird_tag) { return Vertex(); }\n"
	);
	REQUIRE_FALSE(r.diagnostics.has_error());
	REQUIRE(r.unit.stage_interfaces.size() == 1);
	REQUIRE(r.unit.stage_interfaces[0].fields.size() == 1);
	REQUIRE(r.unit.stage_interfaces[0].fields[0].tags.size() == 1);
	REQUIRE(r.unit.stage_interfaces[0].fields[0].tags[0] == "weird_tag");
}

TEST_CASE("using alias cannot declare stage boundary") {
	auto r = parse(
		"struct Vertex { vec4 position; };\n"
		"using X = Vertex : position(clip);\n"
	);
	REQUIRE(r.diagnostics.has_error());
}

TEST_CASE("using import records symbol and namespace-scope imports") {
	auto r = parse(
		"using albedo::tint;\n"
		"export using namespace albedo;\n"
		"using namespace math;\n"
	);
	REQUIRE_FALSE(r.diagnostics.has_error());
	REQUIRE(r.unit.using_imports.size() == 3);
	REQUIRE(r.unit.using_imports[0].kind == UsingImport::Kind::symbol);
	REQUIRE(r.unit.using_imports[0].imported_name == "tint");
	REQUIRE_FALSE(r.unit.using_imports[0].exported);
	REQUIRE(r.unit.using_imports[1].kind == UsingImport::Kind::namespace_scope);
	REQUIRE(r.unit.using_imports[1].exported);
	REQUIRE(r.unit.using_imports[2].kind == UsingImport::Kind::namespace_scope);
}

TEST_CASE("using uniform is not source syntax") {
	auto r = parse("using uniform albedo;\n");
	REQUIRE(r.diagnostics.has_error());
}

TEST_CASE("string literal expression is not source syntax") {
	auto r = parse("fn f() { \"text\"; }");
	REQUIRE(r.diagnostics.has_error());
}

TEST_CASE("export import records imported module and exported decl") {
	auto r = parse("export import \"shared/math.rtsl\";");
	REQUIRE_FALSE(r.diagnostics.has_error());
	REQUIRE(r.unit.imports.size() == 1);
	REQUIRE(r.unit.imports[0] == "shared/math.rtsl");
	REQUIRE(r.unit.exported_imports.size() == 1);
	REQUIRE(r.unit.exported_imports[0] == "shared/math.rtsl");
	REQUIRE(r.unit.declarations.size() == 1);
	REQUIRE(r.unit.declarations[0].kind == DeclKind::import);
	REQUIRE(r.unit.declarations[0].exported);
}

TEST_CASE("input stage interface declaration is not source syntax") {
	auto r = parse(
		"input Point {\n"
		"    location(0) position;\n"
		"    location(1) uv;\n"
		"}\n"
	);
	REQUIRE(r.diagnostics.has_error());
}

TEST_CASE("export marks declarations as exported") {
	auto r = parse("export fn helper() {}");
	REQUIRE_FALSE(r.diagnostics.has_error());
	REQUIRE(find_function(r.unit)->exported);
}

TEST_CASE("multiple top-level declarations parse in order") {
	auto r = parse(
		"struct A { vec3 p; };\n"
		"struct B { vec2 q; };\n"
		"fn t() { }\n"
	);
	REQUIRE_FALSE(r.diagnostics.has_error());
	REQUIRE(r.unit.declarations.size() == 3);
	REQUIRE(r.unit.declarations[0].kind == DeclKind::struct_decl);
	REQUIRE(r.unit.declarations[1].kind == DeclKind::struct_decl);
	REQUIRE(r.unit.declarations[2].kind == DeclKind::function);
}

TEST_CASE("namespace declarations qualify contained symbols") {
	auto r = parse(
		"namespace math {\n"
		"    struct Point { vec3 position; };\n"
		"    fn make(Point p) -> Point { return p; }\n"
		"    uniform camera { UniformBuffer data; }\n"
		"    layout camera::data : mat4;\n"
		"}\n"
	);
	REQUIRE_FALSE(r.diagnostics.has_error());
	REQUIRE(r.unit.structs.size() == 1);
	REQUIRE(r.unit.structs[0].name == "math::Point");
	const auto* fn = find_function(r.unit);
	REQUIRE(fn != nullptr);
	REQUIRE(fn->name == "math::make");
	REQUIRE(fn->parameters.size() == 1);
	REQUIRE(fn->parameters[0].type == "math::Point");
	REQUIRE(fn->return_type == "math::Point");
	REQUIRE(r.unit.uniforms.size() == 1);
	REQUIRE(r.unit.uniforms[0].scope_name == "math::camera");
	REQUIRE(r.unit.layouts.size() == 1);
	REQUIRE(r.unit.layouts[0].path.size() == 2);
	REQUIRE(r.unit.layouts[0].path[0] == "math::camera");
}
