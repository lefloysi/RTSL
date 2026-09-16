#include <rtsl/Frontend/CompilerInstance.hpp>
#include <rtsl/Frontend/ModuleInterface.hpp>
#include <rtsl/Lex/Lexer.hpp>
#include <rtsl/Sema/Sema.hpp>
#include <rtsl/AST/Decl.hpp>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

class ScopedModuleInterfaceFile {
public:
	ScopedModuleInterfaceFile(std::string_view Filename, std::string_view ImportPath)
		: Path(std::filesystem::temp_directory_path() / std::string(Filename)) {
		rtsl::ModuleInterface Interface;
		Interface.units.push_back({.import_path = std::string(ImportPath)});
		const auto Encoded = rtsl::ModuleInterfaceWriter{}.write(Interface);
		if (!Encoded) throw std::runtime_error("failed to encode module interface test fixture");
		std::ofstream Output(Path, std::ios::binary | std::ios::trunc);
		if (!Output) throw std::runtime_error("failed to create module interface test fixture");
		Output.write(reinterpret_cast<const char*>(Encoded.bytes.data()), static_cast<std::streamsize>(Encoded.bytes.size()));
		if (!Output) throw std::runtime_error("failed to write module interface test fixture");
	}

	~ScopedModuleInterfaceFile() { std::error_code Error; std::filesystem::remove(Path, Error); }

	[[nodiscard]] const std::filesystem::path& path() const { return Path; }

private:
	std::filesystem::path Path;
};

} // namespace

TEST_CASE("lexer recognizes every defined RTSL punctuator") {
	struct ExpectedToken {
		rtsl::tok::TokenKind Kind;
		std::string_view Spelling;
	};

	std::vector<ExpectedToken> Expected;
#define PUNCTUATOR(Name, Spelling) Expected.push_back({rtsl::tok::Name, Spelling});
#include <rtsl/Basic/TokenKinds.def>

	std::string Source;
	for (const ExpectedToken& ExpectedToken : Expected) {
		Source.append(ExpectedToken.Spelling);
		Source.push_back(' ');
	}

	rtsl::SourceManager Sources;
	rtsl::IdentifierTable Identifiers;
	rtsl::DiagnosticsEngine Diagnostics;
	const rtsl::FileID File = Sources.createFileID("punctuators.rtsl", Source);
	rtsl::Lexer Lexer(File, Sources, Identifiers, Diagnostics);

	for (const ExpectedToken& ExpectedToken : Expected) {
		rtsl::Token Token;
		Lexer.lex(Token);
		REQUIRE(Token.getKind() == ExpectedToken.Kind);
		REQUIRE(Token.getLength() == ExpectedToken.Spelling.size());
		REQUIRE(rtsl::tok::isPunctuator(Token.getKind()));
		REQUIRE(rtsl::tok::getPunctuatorSpelling(Token.getKind()) == ExpectedToken.Spelling);
	}
	rtsl::Token End;
	Lexer.lex(End);
	REQUIRE(End.is(rtsl::tok::eof));
	REQUIRE_FALSE(Diagnostics.hasErrorOccurred());
}

TEST_CASE("declarations lower through parser and Sema") {
	rtsl::CompilerInvocation Invocation;
	Invocation.setModuleName("example");
	Invocation.setInputName("example.rtsl");
	Invocation.setInputBuffer(R"(
struct Header {
	mat4 transform;
}
struct Point {
	vec3 position;
}
@binding : geometry
var buffer<Header, Point> points;
uniform mat4 model;
storage u32 counter;
@stage : vertex
fn main(Point point) -> Point {
	return point;
}
)");

	rtsl::CompilerInstance Compiler;
	Compiler.setInvocation(std::move(Invocation));
	REQUIRE(Compiler.execute());
	REQUIRE_FALSE(Compiler.getDiagnostics().hasErrorOccurred());
	auto Declaration = Compiler.getASTContext()->getTranslationUnitDecl()->declsBegin();
	unsigned Count = 0;
	for (; Declaration; Declaration = Declaration->getNextDeclInContext()) if (!Declaration->isImplicit()) ++Count;
	REQUIRE(Count == 6);
}

TEST_CASE("uniform variables accept anonymous structure declarations") {
	rtsl::CompilerInvocation Invocation;
	Invocation.setModuleName("anonymous-uniform");
	Invocation.setInputName("anonymous-uniform.rtsl");
	Invocation.setInputBuffer(R"(
uniform struct {
	vec2 center;
	vec2 extent;
	vec4 region;
	vec4 tint;
	f32 texture_scale;
} world_draw;
)");

	rtsl::CompilerInstance Compiler;
	Compiler.setInvocation(std::move(Invocation));
	auto Compilation = Compiler.compileToLinkedRTIR();
	REQUIRE(Compilation.succeeded());
	REQUIRE(Compilation.CodeGeneration.Module.uniforms.size() == 1);
	const auto& Uniform = Compilation.CodeGeneration.Module.uniforms.front();
	const auto* Type = Compilation.CodeGeneration.Module.findType(Uniform.type);
	REQUIRE(Type != nullptr);
	REQUIRE(Type->kind == rtsl::ir::TypeKind::type_structure);
	REQUIRE(Type->members.size() == 5);
	REQUIRE(Compilation.CodeGeneration.Module.strings.get(Type->members[0].name) == "center");
	REQUIRE(Compilation.CodeGeneration.Module.strings.get(Type->members[4].name) == "texture_scale");
}

TEST_CASE("structure type expressions work in templates and unnamed parameters") {
	rtsl::CompilerInvocation Invocation;
	Invocation.setInputName("structure-type-expressions.rtsl");
	Invocation.setInputBuffer(R"(
var buffer<void, struct { vec4 color; }> instances;
fn accept(struct Foo) {}
)");

	rtsl::CompilerInstance Compiler;
	Compiler.setInvocation(std::move(Invocation));
	auto Compilation = Compiler.compileToLinkedRTIR();
	REQUIRE(Compilation.succeeded());
	REQUIRE(Compilation.CodeGeneration.Module.resources.size() == 1);
}

TEST_CASE("Sema identifies registered type names") {
	rtsl::CompilerInvocation Invocation;
	Invocation.setInputName("type-names.rtsl");
	Invocation.setInputBuffer("struct UserType {}");
	rtsl::CompilerInstance Compiler;
	Compiler.setInvocation(std::move(Invocation));
	REQUIRE(Compiler.execute());

	auto* Sema = Compiler.getSema();
	REQUIRE(Sema != nullptr);
	auto& Identifiers = Sema->getIdentifierTable();
	REQUIRE(Sema->isTypeName(&Identifiers.get("u32")));
	REQUIRE(Sema->isTypeName(&Identifiers.get("texture_2d")));
	REQUIRE(Sema->isTypeName(&Identifiers.get("UserType")));
	REQUIRE_FALSE(Sema->isTypeName(&Identifiers.get("not_a_type")));
}

TEST_CASE("triangle strip owns the core emission operators") {
	rtsl::CompilerInvocation Invocation;
	Invocation.setInputName("core-members.rtsl");
	Invocation.setInputBuffer("");
	rtsl::CompilerInstance Compiler;
	Compiler.setInvocation(std::move(Invocation));
	REQUIRE(Compiler.execute());
	auto& Identifiers = Compiler.getSema()->getIdentifierTable();
	auto* TranslationUnit = Compiler.getASTContext()->getTranslationUnitDecl();
	rtsl::RecordDecl* TriangleStrip{};
	unsigned TopLevelOperators{};
	for (auto* Declaration = TranslationUnit->declsBegin(); Declaration; Declaration = Declaration->getNextDeclInContext()) {
		if (Declaration->getKind() == rtsl::DeclKind::decl_record &&
			static_cast<rtsl::RecordDecl*>(Declaration)->getIdentifier() == &Identifiers.get("triangle_strip"))
			TriangleStrip = static_cast<rtsl::RecordDecl*>(Declaration);
		if (Declaration->getKind() == rtsl::DeclKind::decl_function &&
			static_cast<rtsl::FunctionDecl*>(Declaration)->getIdentifier() == &Identifiers.get("operator<-"))
			++TopLevelOperators;
	}
	REQUIRE(TriangleStrip != nullptr);
	unsigned MemberOperators{};
	for (auto* Declaration = TriangleStrip->declsBegin(); Declaration; Declaration = Declaration->getNextDeclInContext())
		if (Declaration->getKind() == rtsl::DeclKind::decl_function &&
			static_cast<rtsl::FunctionDecl*>(Declaration)->getIdentifier() == &Identifiers.get("operator<-"))
			++MemberOperators;
	REQUIRE(MemberOperators == 2);
	REQUIRE(TopLevelOperators == 0);
}

TEST_CASE("Sema bootstrap registers only hidden intrinsic type names") {
	rtsl::ASTContext Context;
	rtsl::DiagnosticsEngine Diagnostics;
	rtsl::IdentifierTable Identifiers;
	rtsl::Sema Sema(Context, Diagnostics, Identifiers);
	REQUIRE(Sema.isTypeName(&Identifiers.get("__vec2")));
	REQUIRE_FALSE(Sema.isTypeName(&Identifiers.get("vec2")));
	REQUIRE_FALSE(Sema.isTypeName(&Identifiers.get("triangle_strip")));
}

TEST_CASE("compiler core declares vector components as normal fields") {
	rtsl::CompilerInvocation Invocation;
	Invocation.setInputName("vector-components.rtsl");
	Invocation.setInputBuffer(R"(
fn components(vec2 two, vec3 three, vec4 four) -> f32 {
	return two.y + three.z + four.w;
}
)");
	rtsl::CompilerInstance Compiler;
	Compiler.setInvocation(std::move(Invocation));
	REQUIRE(Compiler.execute());
	auto& Identifiers = Compiler.getSema()->getIdentifierTable();
	auto* TranslationUnit = Compiler.getASTContext()->getTranslationUnitDecl();
	auto findVector = [&](std::string_view Name) {
		for (auto* Declaration = TranslationUnit->declsBegin(); Declaration; Declaration = Declaration->getNextDeclInContext())
			if (Declaration->getKind() == rtsl::DeclKind::decl_record &&
				static_cast<rtsl::RecordDecl*>(Declaration)->getIdentifier() == &Identifiers.get(Name))
				return static_cast<rtsl::RecordDecl*>(Declaration);
		return static_cast<rtsl::RecordDecl*>(nullptr);
	};
	auto checkComponents = [&](std::string_view VectorName, std::initializer_list<std::string_view> Names) {
		auto* Vector = findVector(VectorName);
		REQUIRE(Vector != nullptr);
		auto Component = Names.begin();
		for (auto* Declaration = Vector->declsBegin(); Declaration; Declaration = Declaration->getNextDeclInContext()) {
			if (Declaration->getKind() != rtsl::DeclKind::decl_field) continue;
			REQUIRE(Component != Names.end());
			REQUIRE(static_cast<rtsl::FieldDecl*>(Declaration)->getIdentifier() == &Identifiers.get(*Component));
			REQUIRE(static_cast<rtsl::FieldDecl*>(Declaration)->getType() ==
				Compiler.getSema()->actOnType({.Name = &Identifiers.get("f32")}));
			++Component;
		}
		REQUIRE(Component == Names.end());
	};
	checkComponents("__vec2", {"x", "y"});
	checkComponents("__vec3", {"x", "y", "z"});
	checkComponents("__vec4", {"x", "y", "z", "w"});
}

TEST_CASE("a variable may declare an incomplete record type") {
	rtsl::CompilerInvocation Invocation;
	Invocation.setInputName("incomplete-record-variable.rtsl");
	Invocation.setInputBuffer("var struct Marker marker;");
	rtsl::CompilerInstance Compiler;
	Compiler.setInvocation(std::move(Invocation));
	REQUIRE(Compiler.execute());
	auto* Sema = Compiler.getSema();
	REQUIRE(Sema != nullptr);
	auto& Identifiers = Sema->getIdentifierTable();
	REQUIRE(Sema->isTypeName(&Identifiers.get("Marker")));
	auto* Value = Sema->actOnIdentifierExpr(&Identifiers.get("marker"), {});
	REQUIRE(Value != nullptr);
	REQUIRE(Value->getType().getTypePtr() == Sema->actOnType({.Name = &Identifiers.get("Marker")}).getTypePtr());
}

TEST_CASE("subscript operators accept multiple arguments") {
	rtsl::CompilerInvocation Invocation;
	Invocation.setInputName("subscript-operator.rtsl");
	Invocation.setInputBuffer(R"(
struct Grid {
	fn operator[](usize x, usize y) -> u32;
}
fn sample(Grid grid) -> u32 {
	return grid[1, 2];
}
)");
	rtsl::CompilerInstance Compiler;
	Compiler.setInvocation(std::move(Invocation));
	REQUIRE(Compiler.execute());
	REQUIRE_FALSE(Compiler.getDiagnostics().hasErrorOccurred());
}

TEST_CASE("tessellation evaluation parameters support a direct const patch reference and type-only settings") {
	rtsl::CompilerInvocation Invocation;
	Invocation.setInputName("tessellation-parameter.rtsl");
	Invocation.setInputBuffer(R"(
struct Vertex {}
@stage : tess_eval
fn main(const isoline_patch<Vertex>& curve, tessellation<equal>) -> Vertex {
}
)");
	rtsl::CompilerInstance Compiler;
	Compiler.setInvocation(std::move(Invocation));
	REQUIRE(Compiler.execute());
	auto* Declaration = Compiler.getASTContext()->getTranslationUnitDecl()->declsBegin();
	while (Declaration && (Declaration->getKind() != rtsl::DeclKind::decl_function ||
		static_cast<rtsl::FunctionDecl*>(Declaration)->getIdentifier()->getName() != "main")) Declaration = Declaration->getNextDeclInContext();
	REQUIRE(Declaration != nullptr);
	REQUIRE(static_cast<rtsl::FunctionDecl*>(Declaration)->getNumTypeOnlyParameters() == 1);
}

TEST_CASE("out-of-line constructors are parsed in their enclosing record") {
	rtsl::CompilerInvocation Invocation;
	Invocation.setInputName("out-of-line-constructor.rtsl");
	Invocation.setInputBuffer(R"(
struct Vertex {
	fn Vertex();
}
fn Vertex::Vertex() {
}
)");
	rtsl::CompilerInstance Compiler;
	Compiler.setInvocation(std::move(Invocation));
	REQUIRE(Compiler.execute());
	REQUIRE_FALSE(Compiler.getDiagnostics().hasErrorOccurred());

	auto* Declaration = Compiler.getASTContext()->getTranslationUnitDecl()->declsBegin();
	while (Declaration && (Declaration->getKind() != rtsl::DeclKind::decl_record ||
		static_cast<rtsl::RecordDecl*>(Declaration)->getIdentifier()->getName() != "Vertex"))
		Declaration = Declaration->getNextDeclInContext();
	REQUIRE(Declaration != nullptr);
	auto* Vertex = static_cast<rtsl::RecordDecl*>(Declaration);
	auto* Constructor = Vertex->declsBegin();
	REQUIRE(Constructor != nullptr);
	REQUIRE(Constructor->getKind() == rtsl::DeclKind::decl_function);
	auto* Function = static_cast<rtsl::FunctionDecl*>(Constructor);
	REQUIRE(Function->getIdentifier() == Vertex->getIdentifier());
	REQUIRE(Function->getDeclContext() == Vertex);
	REQUIRE(Function->getBody() != nullptr);
	REQUIRE(Function->getType().getTypePtr()->getTypeClass() == rtsl::TypeClass::type_named);
	REQUIRE(static_cast<const rtsl::NamedType*>(Function->getType().getTypePtr())->getName() == Vertex->getIdentifier());
}

TEST_CASE("out-of-line constructors resolve calls through the record constructor") {
	rtsl::CompilerInvocation Invocation;
	Invocation.setInputName("out-of-line-constructor-call.rtsl");
	Invocation.setInputBuffer(R"(
struct Vertex {
	fn Vertex(f32 value);
}
fn Vertex::Vertex(f32 value) {
}
fn make() -> Vertex {
	return Vertex(1.0);
}
)");
	rtsl::CompilerInstance Compiler;
	Compiler.setInvocation(std::move(Invocation));
	REQUIRE(Compiler.execute());
	REQUIRE_FALSE(Compiler.getDiagnostics().hasErrorOccurred());
}

TEST_CASE("unknown qualified function owners are diagnosed semantically") {
	rtsl::CompilerInvocation Invocation;
	Invocation.setInputName("unknown-qualified-function-owner.rtsl");
	Invocation.setInputBuffer("fn Missing::function() {}");
	rtsl::CompilerInstance Compiler;
	Compiler.setInvocation(std::move(Invocation));
	REQUIRE_FALSE(Compiler.execute());
	const auto& Diagnostics = Compiler.getDiagnostics().diagnostics();
	REQUIRE(Diagnostics.size() == 1);
	REQUIRE(Diagnostics.front().Message == "qualified function owner does not name a declared record");
}

TEST_CASE("out-of-line functions require a declared record member") {
	rtsl::CompilerInvocation Invocation;
	Invocation.setInputName("undeclared-out-of-line-member.rtsl");
	Invocation.setInputBuffer(R"(
struct Point {
	vec3 position;
	vec4 color;
}
struct Vertex : Position {
	vec4 color;
}
fn Vertex::Vertex(Point point) : Position(vec4(point.position, 1.0)) {
	color = point.color;
}
)");
	rtsl::CompilerInstance Compiler;
	Compiler.setInvocation(std::move(Invocation));
	REQUIRE_FALSE(Compiler.execute());
	const auto& Diagnostics = Compiler.getDiagnostics().diagnostics();
	REQUIRE(Diagnostics.size() == 1);
	REQUIRE(Diagnostics.front().Message == "out-of-line function definition does not name a declared member");
}

TEST_CASE("invalid empty buffer storage is diagnosed") {
	rtsl::CompilerInvocation Invocation;
	Invocation.setInputName("invalid.rtsl");
	Invocation.setInputBuffer("var buffer<void, void> empty;");
	rtsl::CompilerInstance Compiler;
	Compiler.setInvocation(std::move(Invocation));
	REQUIRE_FALSE(Compiler.execute());
}

TEST_CASE("imports use build-supplied logical names without an extension rule") {
	ScopedModuleInterfaceFile Interface{"rtsl-logical-import-test.rtslm", "std/vector"};
	rtsl::CompilerInvocation Invocation;
	Invocation.setInputName("consumer.rtsl");
	Invocation.setInputBuffer("import \"std/vector\";");
	Invocation.addModuleInterface({.ImportPath = "std/vector", .InterfacePath = Interface.path().string()});
	rtsl::CompilerInstance Compiler;
	Compiler.setInvocation(std::move(Invocation));
	REQUIRE(Compiler.execute());
}

TEST_CASE("library imports use a separate build-supplied namespace") {
	ScopedModuleInterfaceFile Interface{"rtsl-library-import-test.rtslm", "std"};
	rtsl::CompilerInvocation Invocation;
	Invocation.setInputName("consumer.rtsl");
	Invocation.setInputBuffer("import <std>;");
	Invocation.addLibraryInterface({.LibraryName = "std", .InterfacePath = Interface.path().string()});
	rtsl::CompilerInstance Compiler;
	Compiler.setInvocation(std::move(Invocation));
	REQUIRE(Compiler.execute());
}

TEST_CASE("imports not supplied by the build are diagnosed") {
	rtsl::CompilerInvocation Invocation;
	Invocation.setInputName("consumer.rtsl");
	Invocation.setInputBuffer("import \"missing/module\";");
	Invocation.addModuleInterface({.ImportPath = "std/vector", .InterfacePath = "standard.rtslm"});
	rtsl::CompilerInstance Compiler;
	Compiler.setInvocation(std::move(Invocation));
	REQUIRE_FALSE(Compiler.execute());
	REQUIRE(Compiler.getDiagnostics().diagnostics().front().Message ==
		"import does not name a translation unit or module interface supplied by the build");
}

TEST_CASE("duplicate build import paths are diagnosed") {
	rtsl::CompilerInvocation Invocation;
	Invocation.setInputName("consumer.rtsl");
	Invocation.setInputBuffer("import \"std/vector\";");
	Invocation.addTranslationUnit({.ImportPath = "std/vector", .InputName = "first.rtsl", .InputBuffer = {}});
	Invocation.addModuleInterface({.ImportPath = "std/vector", .InterfacePath = "second.rtslm"});
	rtsl::CompilerInstance Compiler;
	Compiler.setInvocation(std::move(Invocation));
	REQUIRE_FALSE(Compiler.execute());
	REQUIRE(Compiler.getDiagnostics().diagnostics().back().Message ==
		"build supplies more than one translation unit or module interface for the same import path");
}

TEST_CASE("exported type aliases retain their linkage") {
	rtsl::CompilerInvocation Invocation;
	Invocation.setInputName("export-alias.rtsl");
	Invocation.setInputBuffer("export using VertexIndex = u32;");
	rtsl::CompilerInstance Compiler;
	Compiler.setInvocation(std::move(Invocation));
	REQUIRE(Compiler.execute());
	auto Declaration = Compiler.getASTContext()->getTranslationUnitDecl()->declsBegin();
	while (Declaration && Declaration->getKind() != rtsl::DeclKind::decl_type_alias)
		Declaration = Declaration->getNextDeclInContext();
	REQUIRE(Declaration != nullptr);
	REQUIRE(Declaration->getKind() == rtsl::DeclKind::decl_type_alias);
	REQUIRE(static_cast<rtsl::TypeAliasDecl*>(Declaration)->isExported());
	REQUIRE_FALSE(static_cast<rtsl::TypeAliasDecl*>(Declaration)->hasInternalLinkage());
}

TEST_CASE("unknown types are diagnosed at their type token") {
	rtsl::CompilerInvocation Invocation;
	Invocation.setInputName("unknown-type.rtsl");
	Invocation.setInputBuffer("var Missing value;");
	rtsl::CompilerInstance Compiler;
	Compiler.setInvocation(std::move(Invocation));
	REQUIRE_FALSE(Compiler.execute());
	const auto& Diagnostics = Compiler.getDiagnostics().diagnostics();
	REQUIRE(Diagnostics.size() == 1);
	REQUIRE(Diagnostics.front().Message == "unknown type name");
	const rtsl::PresumedLoc Location = Compiler.getSourceManager().getPresumedLoc(Diagnostics.front().Range.Begin);
	REQUIRE(Location.isValid());
	REQUIRE(Location.Filename == "unknown-type.rtsl");
	REQUIRE(Location.Line == 1);
	REQUIRE(Location.Column == 5);
}

TEST_CASE("unknown generic type bases are diagnosed at their type token") {
	rtsl::CompilerInvocation Invocation;
	Invocation.setInputName("unknown-generic-type.rtsl");
	Invocation.setInputBuffer("var Missing<u32> value;");
	rtsl::CompilerInstance Compiler;
	Compiler.setInvocation(std::move(Invocation));
	REQUIRE_FALSE(Compiler.execute());
	const auto& Diagnostics = Compiler.getDiagnostics().diagnostics();
	REQUIRE(Diagnostics.size() == 1);
	REQUIRE(Diagnostics.front().Message == "unknown type name");
	const rtsl::PresumedLoc Location = Compiler.getSourceManager().getPresumedLoc(Diagnostics.front().Range.Begin);
	REQUIRE(Location.isValid());
	REQUIRE(Location.Filename == "unknown-generic-type.rtsl");
	REQUIRE(Location.Line == 1);
	REQUIRE(Location.Column == 5);
}

TEST_CASE("qualified type names are diagnosed instead of being truncated") {
	rtsl::CompilerInvocation Invocation;
	Invocation.setInputName("qualified-type.rtsl");
	Invocation.setInputBuffer("var vec4::Component value;");
	rtsl::CompilerInstance Compiler;
	Compiler.setInvocation(std::move(Invocation));
	REQUIRE_FALSE(Compiler.execute());
	const auto& Diagnostics = Compiler.getDiagnostics().diagnostics();
	REQUIRE(Diagnostics.size() == 1);
	REQUIRE(Diagnostics.front().Message == "qualified type names are not supported");
	const rtsl::PresumedLoc Location = Compiler.getSourceManager().getPresumedLoc(Diagnostics.front().Range.Begin);
	REQUIRE(Location.isValid());
	REQUIRE(Location.Filename == "qualified-type.rtsl");
	REQUIRE(Location.Line == 1);
	REQUIRE(Location.Column == 9);
}

TEST_CASE("function templates retain parameters and direct specializations") {
	rtsl::CompilerInvocation Invocation;
	Invocation.setInputName("function-template.rtsl");
	Invocation.setInputBuffer(R"(
template<typename T : true, usize N>
fn generic(T a) {}
fn foo<5>();
fn foo<5>() {}
fn main() { foo<5>(); }
)");
	rtsl::CompilerInstance Compiler;
	Compiler.setInvocation(std::move(Invocation));
	REQUIRE(Compiler.execute());
	auto* Declaration = Compiler.getASTContext()->getTranslationUnitDecl()->declsBegin();
	while (Declaration && (Declaration->getKind() != rtsl::DeclKind::decl_function ||
		static_cast<rtsl::FunctionDecl*>(Declaration)->getIdentifier()->getName() != "generic")) Declaration = Declaration->getNextDeclInContext();
	REQUIRE(Declaration != nullptr);
	auto* Generic = static_cast<rtsl::FunctionDecl*>(Declaration);
	REQUIRE(Generic->getNumTemplateParameters() == 2);
	REQUIRE(Generic->templateParameters()[0].Name->getName() == "T");
	REQUIRE(Generic->parameters()[0]->getType().getTypePtr()->getTypeClass() == rtsl::TypeClass::type_template_parameter);
	Declaration = Declaration->getNextDeclInContext();
	REQUIRE(Declaration != nullptr);
	auto* Specialized = static_cast<rtsl::FunctionDecl*>(Declaration);
	REQUIRE(Specialized->getNumTemplateArguments() == 1);
	REQUIRE(Specialized->templateArguments()[0].IntegerValue == 5);
	REQUIRE(Specialized->getBody() != nullptr);
}

TEST_CASE("template constraints reject expressions") {
	rtsl::CompilerInvocation Invocation;
	Invocation.setInputName("expression-template-constraint.rtsl");
	Invocation.setInputBuffer("template<typename T : integral> fn foo(T value) {}");
	rtsl::CompilerInstance Compiler;
	Compiler.setInvocation(std::move(Invocation));
	REQUIRE_FALSE(Compiler.execute());
}

TEST_CASE("Position is owned by the standard library") {
	rtsl::CompilerInvocation Invocation;
	Invocation.setInputName("invalid.rtsl");
	Invocation.setInputBuffer("struct Position { vec4 position; }");
	rtsl::CompilerInstance Compiler;
	Compiler.setInvocation(std::move(Invocation));
	REQUIRE_FALSE(Compiler.execute());
}

TEST_CASE("malformed function parameters recover at the next declaration") {
	rtsl::CompilerInvocation Invocation;
	Invocation.setInputName("malformed.rtsl");
	Invocation.setInputBuffer(R"(
struct Vertex { vec4 color; }
@stage : vertex
fn main(Vertex vertex }) -> vec4 {
	return vertex;
}
struct Later { vec4 color; }
)");
	rtsl::CompilerInstance Compiler;
	Compiler.setInvocation(std::move(Invocation));
	REQUIRE_FALSE(Compiler.execute());
	REQUIRE(Compiler.getDiagnostics().hasErrorOccurred());
	REQUIRE(Compiler.getDiagnostics().diagnostics().size() == 1);
	const rtsl::PresumedLoc Location = Compiler.getSourceManager().getPresumedLoc(
		Compiler.getDiagnostics().diagnostics().front().Range.Begin);
	REQUIRE(Location.isValid());
	REQUIRE(Location.Filename == "malformed.rtsl");
	REQUIRE(Location.Line == 4);
	REQUIRE(Location.Column == 23);
	auto Declaration = Compiler.getASTContext()->getTranslationUnitDecl()->declsBegin();
	while (Declaration && (Declaration->getKind() != rtsl::DeclKind::decl_record ||
		static_cast<rtsl::RecordDecl*>(Declaration)->getIdentifier()->getName() != "Later"))
		Declaration = Declaration->getNextDeclInContext();
	REQUIRE(Declaration != nullptr);
}

TEST_CASE("invalid numeric literal in a function body is diagnosed") {
	rtsl::CompilerInvocation Invocation;
	Invocation.setInputName("invalid-statement.rtsl");
	Invocation.setInputBuffer(R"(
struct Vertex : Position {
	vec4 color;
}
@stage : fragment
fn main(Vertex vertex{.color : flat}) -> vec4 {
	return vertex.color;
	2s;
}
)");
	rtsl::CompilerInstance Compiler;
	Compiler.setInvocation(std::move(Invocation));
	REQUIRE_FALSE(Compiler.execute());
	REQUIRE(Compiler.getDiagnostics().hasErrorOccurred());
	REQUIRE(Compiler.getDiagnostics().diagnostics().front().Message == "invalid numeric literal");
}

TEST_CASE("extra parameter brace produces one parameter diagnostic") {
	rtsl::CompilerInvocation Invocation;
	Invocation.setInputName("extra-parameter-brace.rtsl");
	Invocation.setInputBuffer(R"(
struct Vertex : Position {
	vec4 color;
}
@stage : fragment
fn main(Vertex vertex{.color : flat}}) -> vec4 {
	return vertex.color;
}
)");
	rtsl::CompilerInstance Compiler;
	Compiler.setInvocation(std::move(Invocation));
	REQUIRE_FALSE(Compiler.execute());
	const auto& Diagnostics = Compiler.getDiagnostics().diagnostics();
	REQUIRE(Diagnostics.size() == 1);
	REQUIRE(Diagnostics.front().Message == "expected ')' after parameters");
	const rtsl::PresumedLoc Location = Compiler.getSourceManager().getPresumedLoc(Diagnostics.front().Range.Begin);
	REQUIRE(Location.isValid());
	REQUIRE(Location.Filename == "extra-parameter-brace.rtsl");
	REQUIRE(Location.Line == 6);
	REQUIRE(Location.Column == 37);
}

TEST_CASE("malformed parameter contract reports the unexpected token once") {
	rtsl::CompilerInvocation Invocation;
	Invocation.setInputName("malformed-parameter-contract.rtsl");
	Invocation.setInputBuffer(R"(
struct Vertex : Position {
	vec4 color;
}
@stage : fragment
fn main(Vertex vertex{.color :s flat}) -> vec4 {
	return vertex.color;
}
)");
	rtsl::CompilerInstance Compiler;
	Compiler.setInvocation(std::move(Invocation));
	REQUIRE_FALSE(Compiler.execute());
	const auto& Diagnostics = Compiler.getDiagnostics().diagnostics();
	REQUIRE(Diagnostics.size() == 1);
	REQUIRE(Diagnostics.front().Message == "expected ',' or '}' after parameter contract");
	const rtsl::PresumedLoc Location = Compiler.getSourceManager().getPresumedLoc(Diagnostics.front().Range.Begin);
	REQUIRE(Location.isValid());
	REQUIRE(Location.Line == 6);
}
