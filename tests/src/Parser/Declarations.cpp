#include <rtsl/Frontend/CompilerInstance.hpp>
#include <rtsl/Lex/Lexer.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

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
	for (; Declaration; Declaration = Declaration->getNextDeclInContext()) ++Count;
	REQUIRE(Count == 7);
}

TEST_CASE("invalid empty buffer storage is diagnosed") {
	rtsl::CompilerInvocation Invocation;
	Invocation.setInputName("invalid.rtsl");
	Invocation.setInputBuffer("var buffer<void, void> empty;");
	rtsl::CompilerInstance Compiler;
	Compiler.setInvocation(std::move(Invocation));
	REQUIRE_FALSE(Compiler.execute());
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

TEST_CASE("function template parameters are retained and visible to their definition") {
	rtsl::CompilerInvocation Invocation;
	Invocation.setInputName("function-template.rtsl");
	Invocation.setInputBuffer(R"(
template<typename T>
fn foo(T a) {
}
)");
	rtsl::CompilerInstance Compiler;
	Compiler.setInvocation(std::move(Invocation));
	REQUIRE(Compiler.execute());
	auto Declaration = Compiler.getASTContext()->getTranslationUnitDecl()->declsBegin();
	while (Declaration && (Declaration->getKind() != rtsl::DeclKind::decl_function ||
		static_cast<rtsl::FunctionDecl*>(Declaration)->getIdentifier()->getName() != "foo")) Declaration = Declaration->getNextDeclInContext();
	REQUIRE(Declaration != nullptr);
	auto Function = static_cast<rtsl::FunctionDecl*>(Declaration);
	REQUIRE(Function->isFunctionTemplate());
	REQUIRE(Function->getNumTemplateParameters() == 1);
	REQUIRE(Function->templateParameters()[0]->getName() == "T");
	REQUIRE(Function->parameters()[0]->getType().getTypePtr()->getTypeClass() == rtsl::TypeClass::type_template_parameter);
}

TEST_CASE("function template calls report that instantiation is unavailable") {
	rtsl::CompilerInvocation Invocation;
	Invocation.setInputName("function-template-call.rtsl");
	Invocation.setInputBuffer(R"(
template<typename T>
fn foo(T a) {
}
fn main() {
	foo(1);
}
)");
	rtsl::CompilerInstance Compiler;
	Compiler.setInvocation(std::move(Invocation));
	REQUIRE_FALSE(Compiler.execute());
	const auto& Diagnostics = Compiler.getDiagnostics().diagnostics();
	REQUIRE(Diagnostics.size() == 1);
	REQUIRE(Diagnostics.front().Message == "function template instantiation is not implemented");
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
